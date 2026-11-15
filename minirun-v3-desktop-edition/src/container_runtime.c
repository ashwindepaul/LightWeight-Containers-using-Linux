#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sched.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <limits.h>

#define STACK_SIZE (4 * 1024 * 1024)
#define MAX_BINDS 64
#define MAX_ENVS 64

typedef struct {
    char *src;
    char *dst;
    int readonly;
} BindMount;

typedef struct {
    char *name;
    char *rootfs;
    char *command;
    long memory_limit;
    int cpu_pct;
    int host_runtime;
    int net_namespace;
    int uts_namespace;
    char *hostname;
    char *workdir;
    BindMount binds[MAX_BINDS];
    int bind_count;
    char *envs[MAX_ENVS];
    int env_count;
} ContainerConfig;

static int write_file(const char *path, const char *value) {
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    ssize_t n = write(fd, value, strlen(value));
    close(fd);
    return n < 0 ? -1 : 0;
}

static int mkdir_p(const char *path, mode_t mode) {
    char tmp[PATH_MAX];
    if (!path || strlen(path) >= sizeof(tmp)) return -1;
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, mode);
            *p = '/';
        }
    }
    return mkdir(tmp, mode) == -1 && errno != EEXIST ? -1 : 0;
}

static int bind_mount(const char *src, const char *dst, int readonly) {
    struct stat st;
    if (stat(src, &st) != 0) {
        fprintf(stderr, "bind source '%s' does not exist: %s\n", src, strerror(errno));
        return -1;
    }
    if (S_ISDIR(st.st_mode)) {
        mkdir_p(dst, 0755);
    } else {
        char parent[PATH_MAX];
        if (strlen(dst) >= sizeof(parent)) return -1;
        snprintf(parent, sizeof(parent), "%s", dst);
        char *slash = strrchr(parent, '/');
        if (slash) {
            if (slash == parent) slash[1] = '\0';
            else *slash = '\0';
            if (mkdir_p(parent, 0755) != 0) return -1;
        }
        int fd = open(dst, O_CREAT | O_RDWR, 0644);
        if (fd < 0 && errno != EEXIST) {
            fprintf(stderr, "could not prepare bind target '%s': %s\n", dst, strerror(errno));
            return -1;
        }
        if (fd >= 0) close(fd);
    }
    if (mount(src, dst, NULL, MS_BIND | MS_REC, NULL) != 0) {
        fprintf(stderr, "mount --bind %s -> %s failed: %s\n", src, dst, strerror(errno));
        return -1;
    }
    if (readonly) {
        if (mount(NULL, dst, NULL, MS_BIND | MS_REMOUNT | MS_RDONLY | MS_REC, NULL) != 0) {
            fprintf(stderr, "could not make bind mount '%s' read-only: %s\n", dst, strerror(errno));
            return -1;
        }
    }
    return 0;
}

static int setup_cgroups(const char *name, long mem_bytes, int cpu_pct) {
    if (access("/sys/fs/cgroup/cgroup.controllers", F_OK) != 0) {
        fprintf(stderr, "warning: cgroups v2 is unavailable; CPU/memory limits may not be enforced\n");
        return 0;
    }
    char path[PATH_MAX], buf[128];
    snprintf(path, sizeof(path), "/sys/fs/cgroup/minirun-%s", name);
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "warning: cannot create cgroup '%s': %s; resource limits may not be enforced\n", path, strerror(errno));
        return 0;
    }
    int ok = 1;
    if (write_file("/sys/fs/cgroup/cgroup.subtree_control", "+cpu +memory +pids") != 0 && errno != EBUSY) ok = 0;
    snprintf(path, sizeof(path), "/sys/fs/cgroup/minirun-%s/memory.max", name);
    snprintf(buf, sizeof(buf), "%ld", mem_bytes); if (write_file(path, buf) != 0) ok = 0;
    snprintf(path, sizeof(path), "/sys/fs/cgroup/minirun-%s/memory.swap.max", name);
    if (write_file(path, "0") != 0) fprintf(stderr, "warning: unable to disable swap for '%s'\n", name);
    snprintf(path, sizeof(path), "/sys/fs/cgroup/minirun-%s/cpu.max", name);
    snprintf(buf, sizeof(buf), "%d000 100000", cpu_pct); if (write_file(path, buf) != 0) ok = 0;
    snprintf(path, sizeof(path), "/sys/fs/cgroup/minirun-%s/pids.max", name);
    if (write_file(path, "1024") != 0) ok = 0;
    if (!ok) {
        fprintf(stderr, "warning: one or more cgroup controls could not be applied for '%s'; requested resource limits may not be enforced\n", name);
        snprintf(path, sizeof(path), "/sys/fs/cgroup/minirun-%s", name);
        rmdir(path);
        return 0;
    }
    return 1;
}

static void join_cgroup(const char *name) {
    char path[PATH_MAX], pid[32];
    snprintf(path, sizeof(path), "/sys/fs/cgroup/minirun-%s/cgroup.procs", name);
    snprintf(pid, sizeof(pid), "%d", getpid());
    write_file(path, pid);
}

static void cleanup_cgroup(const char *name) {
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "/sys/fs/cgroup/minirun-%s", name);
    rmdir(path);
}

static int valid_container_name(const char *name) {
    size_t len = name ? strlen(name) : 0;
    if (len == 0 || len > 63 || !((name[0] >= 'A' && name[0] <= 'Z') ||
        (name[0] >= 'a' && name[0] <= 'z') || (name[0] >= '0' && name[0] <= '9'))) return 0;
    for (size_t i = 1; i < len; ++i) {
        char c = name[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return 0;
    }
    return 1;
}

static int setup_rootfs(ContainerConfig *cfg) {
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) != 0) {
        fprintf(stderr, "warning: mount propagation setup failed: %s\n", strerror(errno));
    }

    if (cfg->host_runtime) {
        const char *dirs[] = {"/usr", "/bin", "/sbin", "/lib", "/lib64", "/etc"};
    for (size_t i = 0; i < sizeof(dirs)/sizeof(dirs[0]); ++i) {
            char dst[PATH_MAX];
            snprintf(dst, sizeof(dst), "%s%s", cfg->rootfs, dirs[i]);
            mkdir_p(dst, 0755);
            if (bind_mount(dirs[i], dst, 1) != 0) return -1;
        }
    }

    const char *devices[] = {"null", "zero", "full", "random", "urandom"};
    for (size_t i = 0; i < sizeof(devices)/sizeof(devices[0]); ++i) {
        char src[PATH_MAX], dst[PATH_MAX];
        snprintf(src, sizeof(src), "/dev/%s", devices[i]);
        snprintf(dst, sizeof(dst), "%s/dev/%s", cfg->rootfs, devices[i]);
        if (bind_mount(src, dst, 0) != 0) return -1;
    }

    char proc[PATH_MAX], tmp[PATH_MAX], dev[PATH_MAX], run[PATH_MAX];
    snprintf(proc, sizeof(proc), "%s/proc", cfg->rootfs);
    snprintf(tmp, sizeof(tmp), "%s/tmp", cfg->rootfs);
    snprintf(dev, sizeof(dev), "%s/dev", cfg->rootfs);
    snprintf(run, sizeof(run), "%s/run", cfg->rootfs);
    mkdir_p(proc, 0555); mkdir_p(tmp, 0777); mkdir_p(dev, 0755); mkdir_p(run, 0755);

    for (int i = 0; i < cfg->bind_count; ++i) {
        char dst[PATH_MAX];
        if (cfg->binds[i].dst[0] == '/') snprintf(dst, sizeof(dst), "%s%s", cfg->rootfs, cfg->binds[i].dst);
        else snprintf(dst, sizeof(dst), "%s/%s", cfg->rootfs, cfg->binds[i].dst);
        if (bind_mount(cfg->binds[i].src, dst, cfg->binds[i].readonly) != 0) return -1;
    }
    return 0;
}

static int child_main(void *arg) {
    ContainerConfig *cfg = arg;
    join_cgroup(cfg->name);
    if (cfg->uts_namespace && cfg->hostname) sethostname(cfg->hostname, strlen(cfg->hostname));

    if (setup_rootfs(cfg) != 0) return 1;
    if (chdir(cfg->rootfs) != 0 || chroot(cfg->rootfs) != 0) {
        perror("chroot");
        return 1;
    }
    if (chdir(cfg->workdir ? cfg->workdir : "/") != 0) {
        fprintf(stderr, "warning: workdir '%s' unavailable; using /\n", cfg->workdir ? cfg->workdir : "/");
        chdir("/");
    }

    mkdir("/proc", 0555);
    if (mount("proc", "/proc", "proc", 0, "hidepid=2") != 0)
        fprintf(stderr, "warning: /proc mount failed: %s\n", strerror(errno));
    mkdir("/tmp", 0777);
    mount("tmpfs", "/tmp", "tmpfs", 0, "size=512m");

    const char *term = getenv("TERM");
    char *term_copy = term ? strdup(term) : NULL;
    clearenv();
    setenv("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", 1);
    setenv("HOME", "/root", 1);
    setenv("USER", "root", 1);
    setenv("LOGNAME", "root", 1);
    setenv("SHELL", "/bin/bash", 1);
    setenv("LANG", "C.UTF-8", 1);
    if (term_copy) { setenv("TERM", term_copy, 1); free(term_copy); }
    for (int i = 0; i < cfg->env_count; ++i) {
        char *eq = strchr(cfg->envs[i], '=');
        if (eq) {
            *eq = '\0';
            setenv(cfg->envs[i], eq + 1, 1);
            *eq = '=';
        }
    }
    setenv("MINIRUN_CONTAINER", cfg->name, 1);

    fprintf(stdout, "\nMiniRun container '%s'\nPID: %d\nHostname: %s\n\n", cfg->name, getpid(), cfg->hostname);
    char *argv[] = {"/bin/bash", "-lc", cfg->command, NULL};
    execv("/bin/bash", argv);
    char *sh[] = {"/bin/sh", "-c", cfg->command, NULL};
    execv("/bin/sh", sh);
    perror("exec");
    return 127;
}

static void usage(const char *p) {
    fprintf(stderr, "Usage: %s NAME ROOTFS COMMAND [MEM_MB] [CPU_PCT] [options]\n", p);
    fprintf(stderr, "Options: --host-runtime --workdir PATH --bind SRC:DST[:ro] --env KEY=VALUE --network host|isolated\n");
}

int main(int argc, char **argv) {
    if (argc < 4) { usage(argv[0]); return 2; }
    long mem_mb = argc > 4 ? atol(argv[4]) : 1024;
    int cpu_pct = argc > 5 ? atoi(argv[5]) : 100;
    if (mem_mb < 64) mem_mb = 64;
    if (cpu_pct < 1) cpu_pct = 1;
    if (cpu_pct > 800) cpu_pct = 800;

    ContainerConfig cfg = {0};
    cfg.name = argv[1]; cfg.rootfs = argv[2]; cfg.command = argv[3];
    if (!valid_container_name(cfg.name)) {
        fprintf(stderr, "invalid container name (use 1-63 letters, digits, '.', '_' or '-', starting with a letter or digit)\n");
        return 2;
    }
    cfg.memory_limit = mem_mb * 1024L * 1024L; cfg.cpu_pct = cpu_pct;
    cfg.uts_namespace = 1; cfg.hostname = argv[1]; cfg.workdir = "/";
    for (int i = 6; i < argc; ++i) {
        if (strcmp(argv[i], "--host-runtime") == 0) cfg.host_runtime = 1;
        else if (strcmp(argv[i], "--net-host") == 0) cfg.net_namespace = 0;
        else if (strcmp(argv[i], "--network") == 0 && i + 1 < argc) {
            const char *network = argv[++i];
            if (strcmp(network, "host") == 0) cfg.net_namespace = 0;
            else if (strcmp(network, "isolated") == 0) cfg.net_namespace = 1;
            else { fprintf(stderr, "invalid network mode '%s' (use host or isolated)\n", network); return 2; }
        }
        else if (strcmp(argv[i], "--workdir") == 0 && i + 1 < argc) cfg.workdir = argv[++i];
        else if (strcmp(argv[i], "--bind") == 0 && i + 1 < argc) {
            if (cfg.bind_count >= MAX_BINDS) { fprintf(stderr, "too many binds\n"); return 2; }
            char *spec = strdup(argv[++i]); char *a = strtok(spec, ":"); char *b = strtok(NULL, ":"); char *c = strtok(NULL, ":");
            if (!a || !b) { fprintf(stderr, "invalid bind: %s\n", spec); free(spec); return 2; }
            cfg.binds[cfg.bind_count].src = strdup(a); cfg.binds[cfg.bind_count].dst = strdup(b);
            cfg.binds[cfg.bind_count].readonly = c && strcmp(c, "ro") == 0; cfg.bind_count++; free(spec);
        } else if (strcmp(argv[i], "--env") == 0 && i + 1 < argc) {
            if (cfg.env_count >= MAX_ENVS) { fprintf(stderr, "too many env vars\n"); return 2; }
            cfg.envs[cfg.env_count++] = strdup(argv[++i]);
        } else { fprintf(stderr, "unknown option: %s\n", argv[i]); return 2; }
    }

    struct stat st;
    if (stat(cfg.rootfs, &st) != 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "rootfs '%s' does not exist\n", cfg.rootfs); return 1;
    }
    printf("MiniRun: %s | memory=%ldMB cpu=%d%% host-runtime=%s network=%s\n", cfg.name, mem_mb, cpu_pct, cfg.host_runtime ? "yes" : "no", cfg.net_namespace ? "isolated" : "host");
    int cg = setup_cgroups(cfg.name, cfg.memory_limit, cfg.cpu_pct);
    void *stack = malloc(STACK_SIZE);
    if (!stack) return 1;
    int flags = CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWUTS | SIGCHLD;
    if (cfg.net_namespace) flags |= CLONE_NEWNET;
    pid_t pid = clone(child_main, (char*)stack + STACK_SIZE, flags, &cfg);
    if (pid < 0) { perror("clone"); free(stack); if (cg) cleanup_cgroup(cfg.name); return 1; }
    int status = 0;
    waitpid(pid, &status, 0);
    int rc = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    if (cg) cleanup_cgroup(cfg.name);
    free(stack);
    return rc;
}

package main

import (
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"runtime"
	"time"

	"github.com/gorilla/mux"
)

// ─────────────────────────────────────────────
//  Dynamic project root (resolves correctly
//  regardless of where you run the binary)
// ─────────────────────────────────────────────
var (
	_, callerFile, _, _ = runtime.Caller(0)
	ProjectRoot         = filepath.Dir(filepath.Dir(callerFile))
)

var (
	ContainersDir  = filepath.Join(ProjectRoot, "containers")
	RuntimeBinary  = filepath.Join(ProjectRoot, "bin", "container_runtime")
	DefaultRootFS  = filepath.Join(ProjectRoot, "myroot")
	db             *Database
	useDatabase    bool
	startTime      time.Time
)

const (
	ServerPort      = "8080"
	ServerPortTLS   = "8443"
	ServerVersion   = "1.0.0"
	DefaultCertPath = "/etc/minirun/cert.pem"
	DefaultKeyPath  = "/etc/minirun/key.pem"
)

// ─────────────────────────────────────────────
//  Data models
// ─────────────────────────────────────────────

type Container struct {
	Name      string    `json:"name"`
	RootFS    string    `json:"rootfs"`
	Command   string    `json:"command"`
	MemoryMB  int       `json:"memory_mb"`
	CPUPct    int       `json:"cpu_pct"`
	Status    string    `json:"status"`
	CreatedAt time.Time `json:"created_at"`
}

type CreateRequest struct {
	Name     string `json:"name"`
	RootFS   string `json:"rootfs,omitempty"`
	Command  string `json:"command,omitempty"`
	MemoryMB int    `json:"memory_mb,omitempty"`
	CPUPct   int    `json:"cpu_pct,omitempty"`
}

type APIResponse struct {
	Success bool        `json:"success"`
	Message string      `json:"message"`
	Data    interface{} `json:"data,omitempty"`
	Error   string      `json:"error,omitempty"`
}

// ─────────────────────────────────────────────
//  Response helpers
// ─────────────────────────────────────────────

func ErrorResponse(w http.ResponseWriter, msg string, code int) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(code)
	json.NewEncoder(w).Encode(APIResponse{Success: false, Error: msg})
}

func SuccessResponse(w http.ResponseWriter, msg string, data interface{}) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(http.StatusOK)
	json.NewEncoder(w).Encode(APIResponse{Success: true, Message: msg, Data: data})
}

// ─────────────────────────────────────────────
//  Handlers
// ─────────────────────────────────────────────

func HealthCheckHandler(w http.ResponseWriter, r *http.Request) {
	SuccessResponse(w, "Service is healthy", map[string]interface{}{
		"status":  "healthy",
		"version": ServerVersion,
		"uptime":  time.Since(startTime).String(),
	})
}

func CreateContainerHandler(w http.ResponseWriter, r *http.Request) {
	var req CreateRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		ErrorResponse(w, "Invalid JSON: "+err.Error(), http.StatusBadRequest)
		return
	}
	if req.Name == "" {
		ErrorResponse(w, "Container name is required", http.StatusBadRequest)
		return
	}
	// Apply defaults
	if req.RootFS == "" {
		req.RootFS = DefaultRootFS
	}
	if req.Command == "" {
		req.Command = "/bin/bash"
	}
	if req.MemoryMB == 0 {
		req.MemoryMB = 512
	}
	if req.CPUPct == 0 {
		req.CPUPct = 50
	}

	container := Container{
		Name:      req.Name,
		RootFS:    req.RootFS,
		Command:   req.Command,
		MemoryMB:  req.MemoryMB,
		CPUPct:    req.CPUPct,
		Status:    "created",
		CreatedAt: time.Now(),
	}

	if useDatabase {
		if err := db.CreateContainer(&container); err != nil {
			ErrorResponse(w, "Failed to save container: "+err.Error(), http.StatusInternalServerError)
			return
		}
	} else {
		// File-based storage
		if err := os.MkdirAll(ContainersDir, 0755); err != nil {
			ErrorResponse(w, "Cannot create containers dir", http.StatusInternalServerError)
			return
		}
		configPath := filepath.Join(ContainersDir, req.Name+".json")
		if _, err := os.Stat(configPath); err == nil {
			ErrorResponse(w, "Container '"+req.Name+"' already exists", http.StatusConflict)
			return
		}
		data, _ := json.MarshalIndent(container, "", "  ")
		if err := os.WriteFile(configPath, data, 0644); err != nil {
			ErrorResponse(w, "Failed to write config: "+err.Error(), http.StatusInternalServerError)
			return
		}
	}

	log.Printf("Container '%s' created", req.Name)
	SuccessResponse(w, "Container created", container)
}

func ListContainersHandler(w http.ResponseWriter, r *http.Request) {
	var containers []Container

	if useDatabase {
		var err error
		containers, err = db.ListContainers()
		if err != nil {
			ErrorResponse(w, "DB error: "+err.Error(), http.StatusInternalServerError)
			return
		}
	} else {
		entries, _ := filepath.Glob(filepath.Join(ContainersDir, "*.json"))
		for _, path := range entries {
			data, err := os.ReadFile(path)
			if err != nil {
				continue
			}
			var c Container
			if err := json.Unmarshal(data, &c); err == nil {
				containers = append(containers, c)
			}
		}
	}

	if containers == nil {
		containers = []Container{}
	}
	SuccessResponse(w, fmt.Sprintf("%d container(s)", len(containers)), containers)
}

func GetContainerHandler(w http.ResponseWriter, r *http.Request) {
	name := mux.Vars(r)["name"]
	var container *Container

	if useDatabase {
		var err error
		container, err = db.GetContainer(name)
		if err != nil {
			ErrorResponse(w, "Container '"+name+"' not found", http.StatusNotFound)
			return
		}
	} else {
		data, err := os.ReadFile(filepath.Join(ContainersDir, name+".json"))
		if err != nil {
			ErrorResponse(w, "Container '"+name+"' not found", http.StatusNotFound)
			return
		}
		var c Container
		json.Unmarshal(data, &c)
		container = &c
	}

	SuccessResponse(w, "Container found", container)
}

func DeleteContainerHandler(w http.ResponseWriter, r *http.Request) {
	name := mux.Vars(r)["name"]

	if useDatabase {
		if err := db.DeleteContainer(name); err != nil {
			ErrorResponse(w, "Container '"+name+"' not found", http.StatusNotFound)
			return
		}
	} else {
		configPath := filepath.Join(ContainersDir, name+".json")
		if err := os.Remove(configPath); err != nil {
			ErrorResponse(w, "Container '"+name+"' not found", http.StatusNotFound)
			return
		}
	}

	log.Printf("Container '%s' deleted", name)
	SuccessResponse(w, "Container deleted", map[string]string{"name": name})
}

func StartContainerHandler(w http.ResponseWriter, r *http.Request) {
	name := mux.Vars(r)["name"]
	configPath := filepath.Join(ContainersDir, name+".json")
	data, err := os.ReadFile(configPath)
	if err != nil {
		ErrorResponse(w, "Container '"+name+"' not found", http.StatusNotFound)
		return
	}
	var c Container
	json.Unmarshal(data, &c)

	SuccessResponse(w, "Container start info", map[string]string{
		"note":    "Interactive containers require a terminal — use the CLI",
		"command": fmt.Sprintf("sudo %s %s %s %s %d %d",
			RuntimeBinary, c.Name, c.RootFS, c.Command, c.MemoryMB, c.CPUPct),
		"cli": fmt.Sprintf("sudo ./minirun start %s", c.Name),
	})
}

// ─────────────────────────────────────────────
//  Middleware
// ─────────────────────────────────────────────

func LoggingMiddleware(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		start := time.Now()
		next.ServeHTTP(w, r)
		log.Printf("[%s] %s %s (%v)", r.Method, r.RequestURI, r.RemoteAddr, time.Since(start))
	})
}

func CORSMiddleware(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Access-Control-Allow-Origin", "*")
		w.Header().Set("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS")
		w.Header().Set("Access-Control-Allow-Headers", "Content-Type, Authorization")
		if r.Method == "OPTIONS" {
			w.WriteHeader(http.StatusOK)
			return
		}
		next.ServeHTTP(w, r)
	})
}

// ─────────────────────────────────────────────
//  main()
// ─────────────────────────────────────────────

func main() {
	startTime = time.Now()

	// Optional PostgreSQL (falls back to JSON files)
	dbHost := os.Getenv("DB_HOST")
	dbPort := os.Getenv("DB_PORT")
	dbUser := os.Getenv("DB_USER")
	dbPass := os.Getenv("DB_PASSWORD")
	dbName := os.Getenv("DB_NAME")

	if dbHost != "" && dbUser != "" && dbPass != "" && dbName != "" {
		var err error
		db, err = NewDatabase(dbHost, dbPort, dbUser, dbPass, dbName)
		if err == nil {
			if err = db.InitializeSchema(); err == nil {
				useDatabase = true
				log.Println("✓ Connected to PostgreSQL")
				defer db.Close()
			}
		}
		if !useDatabase {
			log.Println("⚠  DB unavailable — using file storage")
		}
	} else {
		log.Println("ℹ  No DB credentials — using file storage")
		useDatabase = false
	}

	router := mux.NewRouter()
	router.Use(LoggingMiddleware)
	router.Use(CORSMiddleware)

	router.HandleFunc("/",                          func(w http.ResponseWriter, r *http.Request) {
		SuccessResponse(w, "MiniRun Orchestrator API", map[string]interface{}{
			"version": ServerVersion,
			"endpoints": []string{
				"GET    /health",
				"POST   /containers",
				"GET    /containers",
				"GET    /containers/{name}",
				"DELETE /containers/{name}",
				"POST   /containers/{name}/start",
			},
		})
	}).Methods("GET")
	router.HandleFunc("/health",                    HealthCheckHandler).Methods("GET")
	router.HandleFunc("/containers",                CreateContainerHandler).Methods("POST")
	router.HandleFunc("/containers",                ListContainersHandler).Methods("GET")
	router.HandleFunc("/containers/{name}",         GetContainerHandler).Methods("GET")
	router.HandleFunc("/containers/{name}",         DeleteContainerHandler).Methods("DELETE")
	router.HandleFunc("/containers/{name}/start",   StartContainerHandler).Methods("POST")

	certPath := orEnv("TLS_CERT_PATH", DefaultCertPath)
	keyPath  := orEnv("TLS_KEY_PATH", DefaultKeyPath)

	log.Printf("╔══════════════════════════════════════════╗")
	log.Printf("║   MiniRun Container Orchestrator v%s  ║", ServerVersion)
	log.Printf("╚══════════════════════════════════════════╝")

	if fileExists(certPath) && fileExists(keyPath) {
		addr := ":" + ServerPortTLS
		log.Printf("🔒 HTTPS on https://localhost%s", addr)
		log.Fatal(http.ListenAndServeTLS(addr, certPath, keyPath, router))
	} else {
		addr := ":" + ServerPort
		log.Printf("🌐 HTTP  on http://localhost%s", addr)
		log.Printf("   (Set TLS_CERT_PATH + TLS_KEY_PATH to enable HTTPS)")
		log.Fatal(http.ListenAndServe(addr, router))
	}
}

func fileExists(p string) bool { _, err := os.Stat(p); return err == nil }
func orEnv(key, def string) string {
	if v := os.Getenv(key); v != "" { return v }
	return def
}

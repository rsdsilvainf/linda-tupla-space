package main

import (
	"bufio"
	"fmt"
	"net"
	"os"
	"strings"
	"sync"
)

// ===========================================
// DEFINIÇÕES E ESTRUTURAS
// ===========================================

type Tuple struct {
	Key   string
	Value string
}

type WaitingClient struct {
	key string
	ch  chan struct{}
}

// ===========================================
// CLASSE TUPLE SPACE
// ===========================================

type TupleSpace struct {
	tuples         map[string][]Tuple
	services       map[string]func(string) string
	waitingClients []WaitingClient

	mu         sync.Mutex
	waitingMu  sync.Mutex
}

// Construtor
func NewTupleSpace() *TupleSpace {
	ts := &TupleSpace{
		tuples:   make(map[string][]Tuple),
		services: make(map[string]func(string) string),
	}
	ts.registerServices()
	return ts
}

// Registro de serviços
func (ts *TupleSpace) registerServices() {

	ts.services["1"] = func(s string) string {
		return strings.ToUpper(s)
	}

	ts.services["2"] = func(s string) string {
		r := []rune(s)
		for i, j := 0, len(r)-1; i < j; i, j = i+1, j-1 {
			r[i], r[j] = r[j], r[i]
		}
		return string(r)
	}

	ts.services["3"] = func(s string) string {
		return fmt.Sprintf("%d", len(s))
	}

	ts.services["4"] = func(s string) string {
		return s + " " + s
	}
}

// ===========================================
// OPERAÇÕES LINDA
// ===========================================

// WR
func (ts *TupleSpace) write(key, value string) string {
	ts.mu.Lock()
	ts.tuples[key] = append(ts.tuples[key], Tuple{key, value})
	fmt.Println("[WR] Tupla inserida:", "("+key+",", value+")")
	ts.mu.Unlock()

	ts.notifyWaitingClients(key)

	return "OK"
}

// RD
func (ts *TupleSpace) read(key string) string {

	for {
		ts.mu.Lock()

		if list, ok := ts.tuples[key]; ok && len(list) > 0 {
			t := list[0]
			fmt.Println("[RD] Tupla lida:", "("+key+",", t.Value+")")
			ts.mu.Unlock()
			return "OK " + t.Value
		}

		fmt.Println("[RD] Esperando tupla com chave:", key)

		ch := make(chan struct{})
		ts.waitingMu.Lock()
		ts.waitingClients = append(ts.waitingClients, WaitingClient{key, ch})
		ts.waitingMu.Unlock()

		ts.mu.Unlock()

		<-ch
	}
}

// IN
func (ts *TupleSpace) take(key string) string {

	for {
		ts.mu.Lock()

		if list, ok := ts.tuples[key]; ok && len(list) > 0 {
			t := list[0]
			ts.tuples[key] = list[1:]
			if len(ts.tuples[key]) == 0 {
				delete(ts.tuples, key)
			}
			fmt.Println("[IN] Tupla removida:", "("+key+",", t.Value+")")
			ts.mu.Unlock()
			return "OK " + t.Value
		}

		fmt.Println("[IN] Esperando tupla com chave:", key)

		ch := make(chan struct{})
		ts.waitingMu.Lock()
		ts.waitingClients = append(ts.waitingClients, WaitingClient{key, ch})
		ts.waitingMu.Unlock()

		ts.mu.Unlock()

		<-ch
	}
}

// EX
func (ts *TupleSpace) execute(keyIn, keyOut, serviceID string) string {

	for {
		ts.mu.Lock()

		if list, ok := ts.tuples[keyIn]; ok && len(list) > 0 {

			service, exists := ts.services[serviceID]
			if !exists {
				ts.mu.Unlock()
				fmt.Println("[EX] Serviço não encontrado:", serviceID)
				return "NO-SERVICE"
			}

			t := list[0]
			ts.tuples[keyIn] = list[1:]
			if len(ts.tuples[keyIn]) == 0 {
				delete(ts.tuples, keyIn)
			}

			fmt.Println("[EX] Tupla entrada removida:", "("+keyIn+",", t.Value+")")

			result := service(t.Value)

			fmt.Println("[EX] Serviço", serviceID, "aplicado:", t.Value, "->", result)

			ts.tuples[keyOut] = append(ts.tuples[keyOut], Tuple{keyOut, result})

			fmt.Println("[EX] Tupla saída inserida:", "("+keyOut+",", result+")")

			ts.mu.Unlock()

			ts.notifyWaitingClients(keyOut)

			return "OK"
		}

		fmt.Println("[EX] Esperando tupla com chave:", keyIn)

		ch := make(chan struct{})
		ts.waitingMu.Lock()
		ts.waitingClients = append(ts.waitingClients, WaitingClient{keyIn, ch})
		ts.waitingMu.Unlock()

		ts.mu.Unlock()

		<-ch
	}
}

// LIST (igual ao C++)
func (ts *TupleSpace) listTuples() {

	ts.mu.Lock()
	defer ts.mu.Unlock()

	fmt.Println("\n=== ESTADO DO ESPAÇO DE TUPLAS ===")

	if len(ts.tuples) == 0 {
		fmt.Println("(vazio)")
		fmt.Println("=================================")
		return
	}

	for k, list := range ts.tuples {
		fmt.Println("Chave:", k)
		for i, t := range list {
			fmt.Printf("  %d. %s\n", i+1, t.Value)
		}
	}

	fmt.Println("=================================\n")
}

// Notificar clientes esperando
func (ts *TupleSpace) notifyWaitingClients(key string) {

	ts.waitingMu.Lock()
	defer ts.waitingMu.Unlock()

	for i := 0; i < len(ts.waitingClients); {
		if ts.waitingClients[i].key == key {
			close(ts.waitingClients[i].ch)
			ts.waitingClients = append(ts.waitingClients[:i], ts.waitingClients[i+1:]...)
		} else {
			i++
		}
	}
}

// ===========================================
// MANIPULAÇÃO DE CLIENTES
// ===========================================

func handleClient(conn net.Conn, ts *TupleSpace) {
	defer conn.Close()

	addr := conn.RemoteAddr().String()
	fmt.Println("\n[NOVO CLIENTE]", addr)

	reader := bufio.NewReader(conn)

	for {
		line, err := reader.ReadString('\n')
		if err != nil {
			fmt.Println("[CLIENTE DESCONECTADO]", addr)
			return
		}

		line = strings.TrimSpace(line)
		fmt.Println("[COMANDO]", addr, "->", line)

		parts := strings.SplitN(line, " ", 4)
		var response string

		switch parts[0] {

		case "WR":
			if len(parts) < 3 {
				response = "ERROR Formato: WR chave valor"
			} else {
				response = ts.write(parts[1], parts[2])
			}

		case "RD":
			if len(parts) < 2 {
				response = "ERROR Formato: RD chave"
			} else {
				response = ts.read(parts[1])
			}

		case "IN":
			if len(parts) < 2 {
				response = "ERROR Formato: IN chave"
			} else {
				response = ts.take(parts[1])
			}

		case "EX":
			if len(parts) < 4 {
				response = "ERROR Formato: EX chave_entrada chave_saida servico_id"
			} else {
				response = ts.execute(parts[1], parts[2], parts[3])
			}

		case "LIST":
			ts.listTuples()
			response = "OK Listagem no console do servidor"

		case "EXIT":
			conn.Write([]byte("BYE\n"))
			return

		default:
			response = "ERROR Comando desconhecido"
		}

		conn.Write([]byte(response + "\n"))
	}
}

// ===========================================
// MAIN
// ===========================================

func main() {

	port := "54321"
	if len(os.Args) > 1 {
		port = os.Args[1]
	}

	fmt.Println("==========================================")
	fmt.Println("    SERVIDOR LINDA - ESPAÇO DE TUPLAS")
	fmt.Println("    Implementação em Go")
	fmt.Println("==========================================")
	fmt.Println("Serviços disponíveis:")
	fmt.Println("  1 - Converter para MAIÚSCULAS")
	fmt.Println("  2 - Inverter string")
	fmt.Println("  3 - Contar caracteres")
	fmt.Println("  4 - Duplicar string")
	fmt.Println("==========================================")

	ts := NewTupleSpace()

	ln, err := net.Listen("tcp", ":"+port)
	if err != nil {
		fmt.Println("ERRO ao abrir porta:", err)
		return
	}
	defer ln.Close()

	fmt.Println("\n[STATUS] Servidor iniciado na porta", port)
	fmt.Println("[STATUS] Aguardando conexões...")

	for {
		conn, err := ln.Accept()
		if err != nil {
			continue
		}
		go handleClient(conn, ts)
	}
}

#include <iostream>
#include <string>
#include <map>
#include <queue>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <algorithm>
#include <cstring>
#include <sstream>  // ADICIONE ESTA LINHA!
#include <winsock2.h>
#include <ws2tcpip.h>
#include <unordered_map>

#pragma comment(lib, "ws2_32.lib")

// ===========================================
// DEFINIÇÕES E ESTRUTURAS
// ===========================================

struct Tuple {
    std::string key;
    std::string value;
    
    Tuple(const std::string& k, const std::string& v) : key(k), value(v) {}
};

// Estrutura para clientes esperando
struct WaitingClient {
    std::condition_variable* cv;
    std::string key;
    bool is_in;  // true para IN, false para RD
};

// ===========================================
// CLASSE TUPLE SPACE (Espaço de Tuplas)
// ===========================================

class TupleSpace {
private:
    std::map<std::string, std::queue<Tuple>> tuples;
    std::unordered_map<std::string, std::function<std::string(const std::string&)>> services;
    std::vector<WaitingClient> waiting_clients;
    
    std::mutex mtx;
    std::mutex waiting_mtx;
    
public:
    TupleSpace() {
        // Inicializa serviços
        register_services();
    }
    
    // Registra os serviços disponíveis
    void register_services() {
        // Serviço 1: Converter para maiúsculas
        services["1"] = [](const std::string& input) {
            std::string result = input;
            std::transform(result.begin(), result.end(), result.begin(), ::toupper);
            return result;
        };
        
        // Serviço 2: Inverter string
        services["2"] = [](const std::string& input) {
            std::string result = input;
            std::reverse(result.begin(), result.end());
            return result;
        };
        
        // Serviço 3: Contar caracteres
        services["3"] = [](const std::string& input) {
            return std::to_string(input.length());
        };
        
        // Serviço 4: Duplicar string
        services["4"] = [](const std::string& input) {
            return input + " " + input;
        };
    }
    
    // WR: Inserir tupla
    std::string write(const std::string& key, const std::string& value) {
        std::unique_lock<std::mutex> lock(mtx);
        
        tuples[key].push(Tuple(key, value));
        std::cout << "[WR] Tupla inserida: (" << key << ", " << value << ")" << std::endl;
        
        // Notifica clientes esperando por esta chave
        notify_waiting_clients(key);
        
        return "OK";
    }
    
    // RD: Ler tupla (não destrutivo)
    std::string read(const std::string& key) {
        std::unique_lock<std::mutex> lock(mtx);
        
        // Espera até ter uma tupla com a chave
        while (tuples.find(key) == tuples.end() || tuples[key].empty()) {
            std::cout << "[RD] Esperando tupla com chave: " << key << std::endl;
            
            // Adiciona à lista de espera
            std::condition_variable cv;
            {
                std::lock_guard<std::mutex> wlock(waiting_mtx);
                waiting_clients.push_back({&cv, key, false});
            }
            
            cv.wait(lock);
        }
        
        // Retorna a tupla mais antiga (FIFO)
        Tuple& t = tuples[key].front();
        std::cout << "[RD] Tupla lida: (" << key << ", " << t.value << ")" << std::endl;
        
        return "OK " + t.value;
    }
    
    // IN: Ler e remover tupla (destrutivo)
    std::string take(const std::string& key) {
        std::unique_lock<std::mutex> lock(mtx);
        
        // Espera até ter uma tupla com a chave
        while (tuples.find(key) == tuples.end() || tuples[key].empty()) {
            std::cout << "[IN] Esperando tupla com chave: " << key << std::endl;
            
            // Adiciona à lista de espera
            std::condition_variable cv;
            {
                std::lock_guard<std::mutex> wlock(waiting_mtx);
                waiting_clients.push_back({&cv, key, true});
            }
            
            cv.wait(lock);
        }
        
        // Remove a tupla mais antiga (FIFO)
        Tuple t = tuples[key].front();
        tuples[key].pop();
        
        // Se a fila ficar vazia, remove a chave
        if (tuples[key].empty()) {
            tuples.erase(key);
        }
        
        std::cout << "[IN] Tupla removida: (" << key << ", " << t.value << ")" << std::endl;
        
        return "OK " + t.value;
    }
    
    // EX: Executar serviço
    std::string execute(const std::string& key_in, const std::string& key_out, const std::string& service_id) {
        std::unique_lock<std::mutex> lock(mtx);
        
        // 1. Espera até ter uma tupla com key_in
        while (tuples.find(key_in) == tuples.end() || tuples[key_in].empty()) {
            std::cout << "[EX] Esperando tupla com chave: " << key_in << std::endl;
            
            std::condition_variable cv;
            {
                std::lock_guard<std::mutex> wlock(waiting_mtx);
                waiting_clients.push_back({&cv, key_in, true});  // true = IN (remove)
            }
            
            cv.wait(lock);
        }
        
        // 2. Remove a tupla (como IN)
        Tuple t = tuples[key_in].front();
        tuples[key_in].pop();
        if (tuples[key_in].empty()) {
            tuples.erase(key_in);
        }
        
        std::cout << "[EX] Tupla entrada removida: (" << key_in << ", " << t.value << ")" << std::endl;
        
        // 3. Verifica se serviço existe
        if (services.find(service_id) == services.end()) {
            std::cout << "[EX] Serviço não encontrado: " << service_id << std::endl;
            return "NO-SERVICE";
        }
        
        // 4. Executa serviço
        std::string result = services[service_id](t.value);
        std::cout << "[EX] Serviço " << service_id << " aplicado: " 
                  << t.value << " -> " << result << std::endl;
        
        // 5. Insere resultado no espaço
        tuples[key_out].push(Tuple(key_out, result));
        std::cout << "[EX] Tupla saida inserida: (" << key_out << ", " << result << ")" << std::endl;
        
        // 6. Notifica clientes esperando pela chave de saída
        notify_waiting_clients(key_out);
        
        return "OK";
    }
    
    // Lista todas as tuplas (para debug)
    void list_tuples() {
        std::lock_guard<std::mutex> lock(mtx);
        
        std::cout << "\n=== ESTADO DO ESPAÇO DE TUPLAS ===" << std::endl;
        if (tuples.empty()) {
            std::cout << "(vazio)" << std::endl;
            return;
        }
        
        for (const auto& pair : tuples) {
            std::cout << "Chave: " << pair.first << std::endl;
            std::queue<Tuple> temp = pair.second;
            int count = 1;
            while (!temp.empty()) {
                std::cout << "  " << count++ << ". " << temp.front().value << std::endl;
                temp.pop();
            }
        }
        std::cout << "=================================\n" << std::endl;
    }
    
private:
    // Notifica clientes esperando por uma chave específica
    void notify_waiting_clients(const std::string& key) {
        std::lock_guard<std::mutex> wlock(waiting_mtx);
        
        for (auto it = waiting_clients.begin(); it != waiting_clients.end();) {
            if (it->key == key) {
                it->cv->notify_one();
                it = waiting_clients.erase(it);
            } else {
                ++it;
            }
        }
    }
};

// ===========================================
// MANIPULAÇÃO DE CLIENTES
// ===========================================

void handle_client(SOCKET client_socket, TupleSpace& tuple_space) {
    char buffer[1024];
    char client_ip[INET_ADDRSTRLEN];
    sockaddr_in client_addr;
    int addr_len = sizeof(client_addr);
    
    // Obtém informações do cliente
    getpeername(client_socket, (sockaddr*)&client_addr, &addr_len);
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
    int client_port = ntohs(client_addr.sin_port);
    
    std::cout << "\n[NOVO CLIENTE] " << client_ip << ":" << client_port << std::endl;
    
    while (true) {
        // Limpa buffer
        memset(buffer, 0, sizeof(buffer));
        
        // Recebe comando do cliente
        int bytes_received = recv(client_socket, buffer, sizeof(buffer), 0);
        
        if (bytes_received <= 0) {
            std::cout << "[CLIENTE DESCONECTADO] " << client_ip << ":" << client_port << std::endl;
            break;
        }
        
        // Converte para string e remove \r\n
        std::string command(buffer);
        if (!command.empty() && command.back() == '\n') command.pop_back();
        if (!command.empty() && command.back() == '\r') command.pop_back();
        
        std::cout << "[COMANDO] " << client_ip << ":" << client_port << " -> " << command << std::endl;
        
        // Processa comando
        std::string response;
        std::istringstream iss(command);  // AGORA VAI FUNCIONAR!
        std::string op;
        iss >> op;
        
        try {
            if (op == "WR") {
                std::string key, value;
                if (!(iss >> key)) {
                    response = "ERROR Formato: WR chave valor";
                } else {
                    // Pega o resto da linha como valor
                    std::getline(iss, value);
                    if (!value.empty() && value[0] == ' ') {
                        value.erase(0, 1);
                    }
                    response = tuple_space.write(key, value);
                }
                
            } else if (op == "RD") {
                std::string key;
                if (!(iss >> key)) {
                    response = "ERROR Formato: RD chave";
                } else {
                    response = tuple_space.read(key);
                }
                
            } else if (op == "IN") {
                std::string key;
                if (!(iss >> key)) {
                    response = "ERROR Formato: IN chave";
                } else {
                    response = tuple_space.take(key);
                }
                
            } else if (op == "EX") {
                std::string key_in, key_out, service_id;
                if (!(iss >> key_in >> key_out >> service_id)) {
                    response = "ERROR Formato: EX chave_entrada chave_saida servico_id";
                } else {
                    response = tuple_space.execute(key_in, key_out, service_id);
                }
                
            } else if (op == "LIST") {
                tuple_space.list_tuples();
                response = "OK Listagem no console do servidor";
                
            } else if (op == "EXIT") {
                response = "BYE";
                send(client_socket, response.c_str(), response.length(), 0);
                break;
                
            } else {
                response = "ERROR Comando desconhecido. Use: WR, RD, IN, EX, LIST, EXIT";
            }
        } catch (const std::exception& e) {
            response = "ERROR " + std::string(e.what());
        }
        
        // Envia resposta
        response += "\n";
        send(client_socket, response.c_str(), response.length(), 0);
        
        // Pequena pausa para evitar sobrecarga
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    closesocket(client_socket);
}

// ===========================================
// FUNÇÃO PRINCIPAL
// ===========================================

int main(int argc, char* argv[]) {
    std::cout << "==========================================" << std::endl;
    std::cout << "    SERVIDOR LINDA - ESPAÇO DE TUPLAS    " << std::endl;
    std::cout << "    Implementacao em C++ (Windows)       " << std::endl;
    std::cout << "==========================================" << std::endl;
    std::cout << "Servicos disponiveis:" << std::endl;
    std::cout << "  1 - Converter para MAIUSCULAS" << std::endl;
    std::cout << "  2 - Inverter string" << std::endl;
    std::cout << "  3 - Contar caracteres" << std::endl;
    std::cout << "  4 - Duplicar string" << std::endl;
    std::cout << "==========================================" << std::endl;
    
    // Configuração da porta
    int PORT = 54321;
    if (argc > 1) {
        PORT = std::stoi(argv[1]);
    }
    
    // Inicializa Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "ERRO: Falha ao inicializar Winsock" << std::endl;
        return 1;
    }
    
    // Cria socket do servidor
    SOCKET server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == INVALID_SOCKET) {
        std::cerr << "ERRO: Nao foi possivel criar socket" << std::endl;
        WSACleanup();
        return 1;
    }
    
    // Configura endereço
    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    
    // Permite reuso da porta
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
    
    // Bind
    if (bind(server_socket, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        std::cerr << "ERRO: Bind na porta " << PORT << " falhou" << std::endl;
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }
    
    // Listen
    if (listen(server_socket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "ERRO: Listen falhou" << std::endl;
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }
    
    std::cout << "\n[STATUS] Servidor iniciado na porta " << PORT << std::endl;
    std::cout << "[STATUS] Aguardando conexoes de clientes..." << std::endl;
    
    
    // Cria espaço de tuplas (compartilhado entre threads)
    TupleSpace tuple_space;
    
    // Loop principal - aceita conexões
    while (true) {
        sockaddr_in client_addr;
        int addr_len = sizeof(client_addr);
        
        SOCKET client_socket = accept(server_socket, (sockaddr*)&client_addr, &addr_len);
        
        if (client_socket == INVALID_SOCKET) {
            std::cerr << "ERRO: Accept falhou" << std::endl;
            continue;
        }
        
        // Cria thread para atender cliente
        std::thread client_thread(handle_client, client_socket, std::ref(tuple_space));
        client_thread.detach();  // Executa independentemente
    }
    
    // Limpeza (nunca chega aqui no loop infinito)
    closesocket(server_socket);
    WSACleanup();
    
    return 0;
}
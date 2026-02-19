# linda-tupla-space

🧵 Servidor LINDA – Espaço de Tuplas

Implementação do modelo Linda (Tuple Space) em:

🔹 C++ 

🔹 Go

O servidor permite operações clássicas do modelo Linda:

WR – Write (insere tupla)

RD – Read (le e não remove tupla)

IN – Take (le e remove tupla)

EX – Execute a service(executa um servico)

LIST – Lista tuplas (no servidor)

EXIT – Encerrar conexão

📌 Porta Utilizada

Por padrão, o servidor utiliza:

54321


É possível alterar a porta passando como argumento na execução:

C++
servidor.exe 60000

Go
go run main.go 60000

⚙️ Compilação e Execução
🟦 Versão C++ (Windows)
🔧 Requisitos

Windows

g++ (MinGW) ou Visual Studio

Winsock2

🧱 Compilação (MinGW)
g++ servidor.cpp -o servidor.exe -lws2_32 -pthread

▶ Execução
servidor.exe


ou especificando porta:

servidor.exe 54321

🟢 Versão Go
🔧 Requisitos

Go instalado (1.18+)

▶ Execução direta
go run main.go

🧱 Gerar executável
go build -o servidor_go


Executar:

./servidor_go


ou com porta:

./servidor_go 54321

🧠 Operações do Modelo Linda
Comando	Descrição
WR chave valor	Insere tupla
RD chave	Lê sem remover (bloqueia se não existir)
IN chave	Lê e remove (bloqueia se não existir)
EX chave_in chave_out servico	Remove entrada, processa e escreve saída
LIST	Mostra estado no console do servidor
EXIT	Fecha conexão
🔧 Serviços Disponíveis (EX)
ID	Serviço	Descrição
1	MAIÚSCULAS	Converte string para maiúsculas
2	Inverter	Inverte a string
3	Contar	Retorna número de caracteres
4	Duplicar	Repete a string
🌐 Interação via TCP (Telnet ou Netcat)

Você pode testar usando:

Windows (Telnet)
telnet localhost 54321

Linux / WSL
nc localhost 54321

💻 Exemplos Mínimos de Interação
1️⃣ Inserir tupla
WR nome Rafael


Resposta:

OK

2️⃣ Ler sem remover
RD nome


Resposta:

OK Rafael

3️⃣ Ler e remover
IN nome


Resposta:

OK Rafael

4️⃣ Executar serviço

Inserir entrada:

WR texto hello


Executar serviço 1 (maiúsculas):

EX texto resultado 1


Depois ler resultado:

RD resultado


Resposta:

OK HELLO

5️⃣ Listar estado interno
LIST


Resposta no cliente:

OK Listagem no console do servidor


(O estado aparece no console do servidor)

🔒 Comportamento de Bloqueio

As operações RD, IN e EX são bloqueantes:

Se a chave não existir, o cliente fica aguardando

Quando outro cliente executa WR da chave correspondente, o cliente é desbloqueado

Isso implementa o comportamento clássico do modelo Linda.

🏗 Arquitetura

Servidor TCP multi-thread (C++) / multi-goroutine (Go)

Estrutura FIFO por chave

Controle de concorrência com:

mutex (C++)

sync.Mutex (Go)

Lista de clientes bloqueados por chave

📚 Conceito

O modelo Linda é baseado em um espaço de tuplas compartilhado, onde processos comunicam-se indiretamente através de inserção e retirada de tuplas.

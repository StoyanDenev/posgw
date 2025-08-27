#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>
#include <sqlite3.h>

#include <cstring>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <random>

using namespace std;

class TcpClient
{
    const char*         m_address;
    int                 m_port;
    int                 m_socket;
    struct sockaddr_in  m_serverAddress;
    bool                m_connected;
    char                m_buffer[1024];

public:
    vector<string>      m_message;

    TcpClient(const char* address, int port)
    {
        m_address = address;
        m_port = port;
    }

    ~TcpClient()
    {
        if(m_connected) {
            close(m_socket);
        }
    }

    bool Connect()
    {
        if(!m_connected) {
            m_socket = socket(AF_INET, SOCK_STREAM, 0);
            if (m_socket < 0) {
                cerr << "Error: Cannot create Socket" << endl;
                return false;
            }

            m_serverAddress.sin_family = AF_INET;
            m_serverAddress.sin_port = htons(m_port);

            if (inet_pton(AF_INET, m_address, &m_serverAddress.sin_addr) <= 0) {
                cerr << "Error: Invalid address" << endl;
                return false;
            }

            if (connect(m_socket, (struct sockaddr *)&m_serverAddress, sizeof(m_serverAddress)) < 0) {
                cerr << "Error: Connection Failed" << endl;
                return false;
            }

            Send("HELLO|GW|1.0\n");
            if(Receive()) {
                if(m_message[0] == "HELLO" && m_message[1] == "TERM" && m_message[2] == "1.0\n") {
                    m_connected = true;
                } else {
                    close(m_socket);
                    cerr << "Error: Invalid response" << endl;
                    return false;
                }
            } else {
                close(m_socket);
                return false;
            }
        }

        return true;
    }

    void Send(const char* message)
    {
        struct timeval tv;
        tv.tv_sec = 2;
        tv.tv_usec = 0;
        setsockopt(m_socket, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        send(m_socket, message, strlen(message), 0);
    }

    bool Receive()
    {
        struct pollfd fd;
        fd.fd = m_socket;
        fd.events = POLLIN;
        m_message.clear();

        while(true) {
            int result = poll(&fd, 1, 20000); // 3 seconds timeout
            switch(result) {
                case -1:
                    cerr << "Error: recv error" << endl;
                    return false;
                case 0:
                    cerr << "Error: Response timeout" << endl;
                    return false;
                default:
                    if (fd.revents & POLLIN) {
                        int ret = recv(m_socket,m_buffer,sizeof(m_buffer), 0);
                        m_buffer[ret] = 0;
                        stringstream ss(m_buffer);
                        string message_part;

                        while(getline(ss, message_part, '|')) {
                            m_message.push_back(message_part);
                        }

                        m_buffer[0] = 0;

                        return true;
                    }
            }
        }
    }
};

class SqliteStore
{
    int         m_rc;
    sqlite3*    m_pSqlite;
    char*       m_errMsg;
    bool        m_connected;
public:
    SqliteStore()
    {
        m_errMsg = 0;
        m_connected = false;
        m_rc = sqlite3_open("transactions.db", &m_pSqlite);

        if(m_rc) {
            cerr << "Error: Can't open database: " << sqlite3_errmsg(m_pSqlite) << endl;
            return;
        } else {
            m_connected = true;
        }

        m_rc = sqlite3_exec(m_pSqlite, "create table if not exists transactions (amount REAL, status CHAR(16), reason CHAR(64));", 0, 0, &m_errMsg);
        if( m_rc != SQLITE_OK ){
            cerr << "SQL error: " << m_errMsg << endl;
            sqlite3_free(m_errMsg);
        }
    }

    bool executeSQL(const char* sql)
    {
        m_rc = sqlite3_exec(m_pSqlite, sql, 0, 0, &m_errMsg);
        if( m_rc != SQLITE_OK ){
            cerr << "SQL error: " << m_errMsg << endl;
            sqlite3_free(m_errMsg);

            return false;
        }

        return true;
    }

    void printResults(const char* sql)
    {
        sqlite3_stmt *stmt;
        sqlite3_prepare_v2(m_pSqlite, sql, -1, &stmt, NULL);

        while (sqlite3_step(stmt) != SQLITE_DONE) {
            int i;
            int num_cols = sqlite3_column_count(stmt);
            
            for (i = 0; i < num_cols; i++) {
                switch (sqlite3_column_type(stmt, i))
                {
                    case (SQLITE3_TEXT):
                        printf("%s, ", sqlite3_column_text(stmt, i));
                        break;
                    case (SQLITE_FLOAT):
                        printf("%g, ", sqlite3_column_double(stmt, i));
                        break;
                    default:
                        break;
                }
            }
            printf("\n");
        }

        sqlite3_finalize(stmt);
    }

    ~SqliteStore()
    {
        sqlite3_close(m_pSqlite);
    }
};

class OpiLiteSession
{
    TcpClient*      m_pClient;
    SqliteStore*    m_pDB;

public:
    string          m_status;
    string          m_reason;

    OpiLiteSession()
    {
        m_pClient = 0;
        m_pDB = new SqliteStore();
    }

    ~OpiLiteSession()
    {
        if(m_pClient) {
            delete m_pClient;
        }

        if(m_pDB) {
            delete m_pDB;
        }
    }

    bool Send(const char* amount, const char* host, int port)
    {
        if(0 == m_pClient) {
            m_pClient = new TcpClient(host, port);
        }

        if(m_pClient->Connect()) {
            stringstream ss;
            ss << "AUTH|";
            ss << amount << "|";
            ss << time(NULL) << "|";

            random_device rd;
            mt19937 gen(rd());
            uniform_int_distribution<> distrib(0, 255);
            for(int i = 0; i < 8; i++) {
                int randomValue = distrib(gen);
                ss << hex << randomValue;
            }
            ss << endl;

            m_pClient->Send(ss.str().c_str());
            if(m_pClient->Receive()) {
                m_status = m_pClient->m_message[0];
                if(m_status != "APPROVED") {
                    m_reason = m_pClient->m_message[1];
                }
            } else {
                m_status = "DECLINED";
                m_reason = "TIMEOUT";
            }

            return true;
        } else {
            m_status = "DECLINED";
            m_reason = "CANNOT CONNECT";
        }

        return false;
    }

    void Store(const char* amount)
    {
        stringstream ss;
        ss << "insert into transactions values (";
        ss << amount << ",";
        ss << "'" << m_status << "',";
        ss << "'" << m_reason << "'";
        ss << ");";

        m_pDB->executeSQL(ss.str().c_str());
    }

    void Last(unsigned int count)
    {
        stringstream ss;
        ss << "select * from transactions order by rowid desc limit " << count;

        m_pDB->printResults(ss.str().c_str());
    }

    void Recon()
    {

    }
};

int main(int argc, char * argv[])
{
    if(argc < 2) {
        cout << "Error: Missing second argument" << endl;
        return 1;
    }

    OpiLiteSession* pSession = new OpiLiteSession();
    string command(argv[1]);

    if(command == "sale") {
        if(argc < 8) {
            cout << "Error: Missing arguments" << endl;
            delete pSession;
            return 1;
        }

        string amount("");
        string host("");
        int port = 0;

        for(int i = 2; i < 7; i += 2) {
            string param(argv[i]);
            string value(argv[i + 1]);

            if(param == "--amount") {
                amount = value;
            } else if(param == "--host") {
                host = value;
            } else if(param == "--port") {
                port = stoi(value);
            }
        }

        if(0 == port || 0 == host.length() || 0 == amount.length()) {
            cerr << "Error: Missing parameters" << endl;
            delete pSession;
            return 1;
        }

        bool success = pSession->Send(amount.c_str(), host.c_str(), port);
        pSession->Store(amount.c_str());

        if(!success) {
            cerr << "Error: Auth declined" << endl;
            delete pSession;
            return 1;
        }
    } else if(command == "last") {
        string param(argv[2]);
        string value(argv[3]);
        int count = 0;
        count = stoi(value);

        if(param != "--n" || count < 1) {
            cerr << "Error: Missing parameters" << endl;
            delete pSession;
            return 1;
        } else {
            pSession->Last(count);
        }
    } else if(command == "recon") {
        cout << "recon\n";
    }

    delete pSession;
    return 0;
}


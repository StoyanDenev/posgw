#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>

#include <cstring>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>

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

        cout << "Send: " << message << endl;

        send(m_socket, message, strlen(message), 0);
    }

    bool Receive()
    {
        struct pollfd fd;
        fd.fd = m_socket;
        fd.events = POLLIN;

        while(true) {
            int result = poll(&fd, 1, 10000); // 3 seconds timeout
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
                        cout << "Receive: " << m_buffer << endl;
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

/*! \file 
    \brief ESP Device Finder File
    
    This is to test the documentation of Device Finder.  
    ff
*/

#include "ESPdeviceFinder.h"

extern "C"
{
#include <include/wl_definitions.h>
#include <osapi.h>
#include <ets_sys.h>
}

//#include "debug.h"
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <lwip/opt.h>
#include <lwip/udp.h>
#include <lwip/inet.h>
#include <lwip/igmp.h>
#include <lwip/mem.h>
#include <include/UdpContext.h>

#define UDP_PING_TIMEOUT 1 * 60 * 1000   //  1min ping interval
#define UDP_TASK_TIMEOUT 10 * 1000       //  10sec tasker
#define UDP_STALE_TIMEOUT 2 * 60 * 1000 // 2min stale remove...

static const IPAddress ESPdeviceFinder_MULTICAST_ADDR(224, 0, 0, 251);


ESPdeviceFinder::UDP_item::UDP_item(IPAddress ip, std::unique_ptr<char[]>(ID))
        : IP(ip)
        , name( std::move(ID))
{
        // if (ID) {
        //         size_t len = strnlen(ID, 33);
        //         name = std::unique_ptr<char[]>(new char[len + 1]);
        //         strncpy( name.get() , ID, 33 );
        // }
        // if (app) {
        //         size_t len = strnlen(app, 33);
        //         name = std::unique_ptr<char[]>(new char[len + 1]);
        //         strncpy( name.get() , app, 33 );
        // }
        DEBUGDEVICEFINDERF("%s  (%u.%u.%u.%u)\n", name.get() , IP[0], IP[1], IP[2], IP[3]);
        lastseen = millis();
};

ESPdeviceFinder::UDP_item::~UDP_item()
{
        DEBUGDEVICEFINDERF("~%s  (%u.%u.%u.%u)\n", name.get(), IP[0], IP[1], IP[2], IP[3]);
}




ESPdeviceFinder::ESPdeviceFinder(): _addr(ESPdeviceFinder_MULTICAST_ADDR)
{

        //     WiFiEventHandler onStationModeGotIP(std::function<void(const WiFiEventStationModeGotIP&)>);

        // if (WiFi.isConnected()) {
        //         _state = _udp.beginMulticast( WiFi.localIP(),  MELVANIMATE_MULTICAST_ADDR, _port);
        // }




}

ESPdeviceFinder::~ESPdeviceFinder()
{
        end();
}

void ESPdeviceFinder::end()
{
        DEBUGDEVICEFINDERF("\n");
        if (_conn) {
                DEBUGDEVICEFINDERF("con unrefed\n");
                _conn->unref();
                _conn = nullptr;
        }
        _initialized = false;
}

void ESPdeviceFinder::begin(const char * host, uint16_t port)
{
        if (host) {
                _host = host;
        }

        if (port) {
                _port = port;
        } else {
                _port = DEFAULT_ESPDEVICE_PORT;
        }

        _begin();

}


void ESPdeviceFinder::_begin()
{
        DEBUGDEVICEFINDERF("\n");

        if (_host.length() == 0 ) {
                char tmp[15];
                sprintf_P(tmp, PSTR("esp8266-%06x"), ESP.getChipId());
                _host = tmp;
        }

        if (!_port) {
                _port = DEFAULT_ESPDEVICE_PORT;
        }

        if(WiFi.isConnected()) {
             _restart();   
        } 

        if (!_gotIPHandler) {
                _gotIPHandler = WiFi.onStationModeGotIP([this](const WiFiEventStationModeGotIP & event) {
                        DEBUGDEVICEFINDERF("Got IP\n");
                                _restart();
                });
        }

        if (!_disconnectedHandler) {
                _disconnectedHandler = WiFi.onStationModeDisconnected([this](const WiFiEventStationModeDisconnected & event) {
                        DEBUGDEVICEFINDERF("STA Disconnected\n");
                        if (_initialized) {
                                end();
                        }
                });
        }

        DEBUGDEVICEFINDERF("Finished\n");
}

void ESPdeviceFinder::setHost(String host )
{
        _host = host;
        if (_initialized) {
                _restart();
        }
}

void ESPdeviceFinder::setAppName(String appName)
{
        _appName = appName;
        if (_initialized) {
                _restart();
        }
}

void ESPdeviceFinder::setPort(uint16_t port )
{
        _port = port;
        if (_initialized) {
                _restart();
        }

}

void ESPdeviceFinder::_onRx()
{

        if (!_conn->next()) { return; }
        _parsePacket();

}

void ESPdeviceFinder::_restart()
{
        DEBUGDEVICEFINDERF("\n");
        end();
        _listen();
        _sendRequest(PING);
}

void ESPdeviceFinder::loop()
{

        // if (_udp.parsePacket()) {
        //         _parsePacket();
        // }

        // if (millis() - _lastmessage > 10000) {
        //         _sendRequest(PONG);
        //         _lastmessage = millis();
        // }

#ifdef UDP_TEST_SENDER
        _test_sender();
#endif

        if (devices.size()) {

                uint32_t last_PING = (**devices.begin()).lastseen;

                if (millis() - _checkTimeOut > UDP_TASK_TIMEOUT) {
                        // purge stale entries
                        for (UDPList::iterator it = devices.begin(); it != devices.end(); ++it) {

                                UDP_item & item = **it;

                                if (item.lastseen > last_PING) {
                                        last_PING = item.lastseen;
                                }

                                if (  millis() - item.lastseen > UDP_STALE_TIMEOUT) {

                                        //  std::delete me from the list.....
                                        DEBUGDEVICEFINDERF("Removed %s (%u.%u.%u.%u) not seen for %u\n", item.name.get(), item.IP[0], item.IP[1], item.IP[2], item.IP[3], millis() - item.lastseen );
                                        //DEBUGDEVICEFINDERF("     NEED TO ADD DELETE \n");
                                        it = devices.erase(it);
                                        _sendRequest(PING);
                                }
                        }

                        // get earliest last PING...

                        if ( millis() - last_PING > UDP_PING_TIMEOUT && !_sendPong) {
                                DEBUGDEVICEFINDERF("Sending PING\n");
                                _sendRequest(PING);
                        }

                        //only run the loop every so many seconds...
                        _checkTimeOut = millis();
                }
        } else {

                //  send out periodic pings if non are found but only if you are actually checking for other devices.... 
                if (_cacheResults && millis() - _checkTimeOut > UDP_TASK_TIMEOUT) {
                        DEBUGDEVICEFINDERF("No Devices found sending PING\n");
                        _sendRequest(PING);
                        _checkTimeOut = millis();
                }


        }

//  send replies with delay
        if (_sendPong && _sendPong < millis()) {
                _sendRequest(PONG);
                _sendPong = 0;
        }
}


bool ESPdeviceFinder::_listen()
{
        DEBUGDEVICEFINDERF("\n");

        if (_conn) {
                return false;
        }

        ip_addr_t ifaddr;
        ifaddr.addr = (uint32_t) WiFi.localIP();
        ip_addr_t multicast_addr;
        multicast_addr.addr = (uint32_t) _addr;

        if (igmp_joingroup(&ifaddr, &multicast_addr) != ERR_OK) {
                return false;
        }

        _conn = new UdpContext;

        if (_conn) {

                _conn->ref();

                if (!_conn->listen(IP_ADDR_ANY, _port)) { return false; }

                _conn->onRx(std::bind(&ESPdeviceFinder::_onRx, this));

                _initialized = true;
                return true;

        }

        return false; 
}

// struct UDP_MSG {
//         UDP_REQUEST_TYPE method;
//         uint16_t port;
//         uint32_t ip[4];
//         uint8_t host_len;
//         uint8_t app_len;

// }

void ESPdeviceFinder::_parsePacket()
{
        DEBUGDEVICEFINDERF("_parsePacket\n"); 

        IPAddress IP;

        if (!_conn) {
                return;
        }

        size_t size = _conn->getSize();

        std::unique_ptr<char[]>packet(new char[size]);

        if (packet) {
                _conn->read( packet.get(), size); //  read the whole lot into buffer 
                DEBUGDEVICEFINDERF("Got Packet Length: %u : ", size); 
                _dumpMem(packet.get(), size); 
                _conn->flush();
        } else {
                _conn->flush();
                return; 
        }

        uint8_t XOR{0};

        for (size_t i = 1; i < size ; i++) {
                XOR ^= packet[i];
        }

        if (XOR != packet[0]) {
                DEBUGDEVICEFINDERF("XOR mismatch:  got %u expected %u\n", XOR, packet[0]); 
                return;
        }

        UDP_REQUEST_TYPE method = *reinterpret_cast<UDP_REQUEST_TYPE *>(&packet[1]);  //byte 1

        uint16_t __attribute__((__unused__)) port = *reinterpret_cast<uint16_t*>(&packet[2]); // not currently used, but is in debug 

        IP = *reinterpret_cast<uint32_t*>(&packet[4]) ; 
        uint8_t host_len = packet[8];  // byte 8
        uint8_t app_len = packet[9]; // byte 9


        std::unique_ptr<char[]>buf;

        if (host_len && host_len < 32) {
                buf = std::unique_ptr<char[]>(new char[host_len + 1]);
                if (buf) {
                        memcpy( buf.get(), &packet[10], host_len); // bytes 10 -> host_len;
                        buf[host_len] = '\0';
                }
        } else {
                return; 
        }

        std::unique_ptr<char[]>bufappname;

        if (app_len && app_len < 32) {
                bufappname = std::unique_ptr<char[]>(new char[app_len + 1]);
                if (bufappname) {
                        memcpy( bufappname.get(), &packet[10 + host_len], app_len); // bytes 10 -> host_len;
                        bufappname[app_len] = '\0';
                }
        } else {
                static const char * mynullstr = "null";
                size_t len = strlen(mynullstr); 
                bufappname = std::unique_ptr<char[]>(new char[len + 1]);
                if (bufappname) {
                        memcpy( bufappname.get(), mynullstr, len); // bytes 10 -> host_len;
                        bufappname[len + 1] = '\0';
                }                
        }

        DEBUGDEVICEFINDERF("UDP RECIEVED [%u] %s (%u.%u.%u.%u:%u) %s (%s)\n", millis(), (method == PING) ? "PING" : "PONG", IP[0], IP[1], IP[2], IP[3], port, buf.get(), (bufappname) ? bufappname.get() : "null");
        
        if (_cacheResults) {
                if (  (_filterByAppName && app_len && _appName.length() && _appName == String(bufappname.get())) | !_filterByAppName ) 
                {
                        _addToList(IP, std::move(buf));
                } 

                
        }
        
        if (method == PING) {
                DEBUGDEVICEFINDERF("Recieved PING: RESPONDING WITH PONG\n");
                _sendPong = millis() + random(0, 50);
        }
}

void ESPdeviceFinder::setMulticastIP(IPAddress addr)
{
        _addr = addr;

        if (_initialized) {
                end();
                _listen();
        }
}


//This won't work at the moment.  Packet format has changed!
void ESPdeviceFinder::_sendRequest(UDP_REQUEST_TYPE method)
{

        DEBUGDEVICEFINDERF("type = %s\n", (method == PING) ? "PING" : "PONG");

        if (!_conn || !_initialized) {
                DEBUGDEVICEFINDERF(" failed\n");
                return;
        }

        ip_addr_t mcastAddr;
        mcastAddr.addr = _addr;

        IPAddress IP = WiFi.localIP();

        if (!_conn->connect(&mcastAddr, _port)) {
                return;
        }

        ip_addr_t ifAddr;
        ifAddr.addr = WiFi.localIP();

        _conn->setMulticastInterface( ifAddr ) ;
        _conn->setMulticastTTL(1);

        const char ip[4] = { IP[0], IP[1], IP[2], IP[3] };

        /*  Append Data */ 

        size_t appName_len = _appName.length();
        size_t host_len = _host.length();

        size_t packet_size = appName_len + host_len + 10; 

        std::unique_ptr<char[]>packetbuffer(new char[packet_size]);

        char * buf = packetbuffer.get(); 
        uint16_t position = 1; 


        memcpy(buf + position, &method, 1 );     /* byte 1   */ position++; 
        memcpy(buf + position, &_port, 2);       /* byte 2,3   */ position += 2; 
        memcpy(buf + position, &ip, 4);          /* byte 4,5,6,7   */ position += 4; 
        memcpy(buf + position, &host_len, 1); /* byte 8   */ position++;
        memcpy(buf + position, &appName_len, 1);    /* byte 9   */ position++; 

        if (host_len && host_len < 33) {
                //DEBUGDEVICEFINDERF("host_len = %u, host const char * addr = %p, _host = %s\n", host_len, _host.c_str(), _host.c_str());
                memcpy( buf + position, _host.c_str(), host_len );  position += host_len; 
        }

        if (appName_len && appName_len < 33) {
                //DEBUGDEVICEFINDERF("appName_len = %u, appName const char * addr = %p, _appName = %s\n", appName_len, _appName.c_str(), _appName.c_str());
                memcpy( buf + position, _appName.c_str(), appName_len);  position += appName_len; 
        }
        // 


        /*  Calculate XOR */ 

        uint8_t XOR{0};

        for (size_t i = 1; i < packet_size ; i++) {
                XOR ^= buf[i];
        }

        buf[0] = XOR; 

        // DEBUGDEVICEFINDERF("Send Packet Length: %u : ", packet_size); 
        // _dumpMem(buf, packet_size);  

        _conn->append(buf, packet_size); 
        bool result = _conn->send();

        if (!result) {
               DEBUGDEVICEFINDERF("ERROR sending Packet\n");  
        }

}

void ESPdeviceFinder::_addToList(IPAddress IP, std::unique_ptr<char[]>(ID) )
{

        if (!ID) {
                return;
        }

        if (strlen(ID.get()) > 33 ) {
                return;
        }

        for (UDPList::iterator it = devices.begin(); it != devices.end(); ++it) {
                //UDP_item & item = *(*it).get();
                UDP_item & item = **it;

                if (IP == item.IP) {
                        //  if name has changed.  update the record..
                        if (ID && strcmp(ID.get(), item.name.get() )  )  {
                                DEBUGDEVICEFINDERF("name different reassigning %s->%s\n", item.name.get(), ID.get());
                                std::unique_ptr<char[]> p( new char[strlen(ID.get()) + 1]  );
                                item.name = std::move(p);
                                strcpy(  item.name.get(), ID.get() );
                        }
                        //  if the appName has changed update the record. 
                        // if (appName && strcmp(appName.get(), item.appName.get() )  )  {
                        //         DEBUGDEVICEFINDERF("[UDP_broadcast::_addToList] name different reassigning %s->%s\n", item.name.get(), ID.get());
                        //         std::unique_ptr<char[]> p( new char[strlen(appName.get()) + 1]  );
                        //         item.appName = std::move(p);
                        //         strcpy(  item.appName.get(), appName.get() );
                        // }

                        item.lastseen = millis(); //  last seen time set whenever packet recieved....
                        return;
                }
        }

        devices.push_back( std::unique_ptr<UDP_item> (new UDP_item(IP, std::move(ID) ) )  );

}

uint8_t ESPdeviceFinder::count()
{
        return devices.size();
}


const char * ESPdeviceFinder::getName(uint8_t i)
{
        uint8_t count = 0;

        for (UDPList::iterator it = devices.begin(); it != devices.end(); ++it) {
                if (count == i) {
                        UDP_item & udp_item = **it;
                        return udp_item.name.get();
                }
                count++;
        }

    return nullptr; 
}

// const char * ESPdeviceFinder::getAppName(uint8_t i)
// {
//         uint8_t count = 0;

//         for (UDPList::iterator it = devices.begin(); it != devices.end(); ++it) {
//                 if (count == i) {
//                         UDP_item & udp_item = **it;
//                         return udp_item.appName.get();
//                 }
//                 count++;
//         }
// }


IPAddress ESPdeviceFinder::getIP(uint8_t i)
{
        uint8_t count = 0;

        for (UDPList::iterator it = devices.begin(); it != devices.end(); ++it) {
                if (count == i) {
                        UDP_item & udp_item = **it;
                        return udp_item.IP;
                }
                count++;
        }
        return INADDR_NONE; 
}


#ifdef UDP_TEST_SENDER

void ESPdeviceFinder::_test_sender()
{
        static uint32_t timeout = 0;
        static uint16_t number = 0;
        UDP_REQUEST_TYPE method = PING;

        if (millis() - timeout > 10000) {

                timeout = millis();

                if (_udp.beginPacketMulticast(ESPdeviceFinder_MULTICAST_ADDR, _port, WiFi.localIP())) {

                        char buf[32] = {'\0'};

                        //strcpy(buf, "Test Sender ");

                        snprintf(buf, 32, "Test Sender %u", number++);

                        const char ip[4] = { 192, 168, 1, (uint8_t)random (1, 255) };
                        method = PONG;

                        _udp.write(reinterpret_cast<const uint8_t *>(&method), 1);
                        _udp.write(reinterpret_cast<const uint8_t *>(&_port), 2);
                        _udp.write( ip, 4);

                        if (_host) {
                                //DEBUGDEVICEFINDERF("[UDP_broadcast::_sendRequest] host = %s\n", _host);
                                uint8_t host_len = strlen(buf);
                                _udp.write(reinterpret_cast<const uint8_t *>(&host_len), 1);
                                _udp.write(buf, host_len + 1);
                        } else {
                                //DEBUGDEVICEFINDERF("[UDP_broadcast::_sendRequest] No Host\n");
                                const char * no_host = "No Host";
                                _udp.write(reinterpret_cast<const uint8_t *>(&no_host), strlen(no_host) + 1);
                        }

                        if (_udp.endPacket()) {
                                DEBUGDEVICEFINDERF(" success\n");
                                return;
                        }
                }
        }

}

#endif // UDP_TEST_SENDER

void ESPdeviceFinder::cacheResults(bool val) {
        _cacheResults = val;
        if (!_cacheResults) {
                clearResults(); 
        }
}

void ESPdeviceFinder::clearResults() {
        devices.clear();
}

void ESPdeviceFinder::_dumpMem(void *mem, size_t size) {
  unsigned char *p __attribute__((unused)) = (unsigned char *)mem;
  for ( uint i = 0 ; i < size; i++) {
    DEBUGDEVICEFINDERF("%02x ", p[i]);
    #ifdef DebugUDP
    if (i && i % 32 == 0) { DebugUDP.println(); } 
    #endif
  }
  DEBUGDEVICEFINDERF("\n");
}


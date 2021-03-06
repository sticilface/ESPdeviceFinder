/*! \file 
    \brief ESP Device Finder File
    
    UDP finder and storer of found devices.
    Port set in Melvanimate.h

    Sends ping every 5min unless pin recieved , should linit the traffic..
    Adds IP address and name to json array,
    First attempt at using std::smart pointers and lists
*/

/*



    ToDo
    1)  Possible bug where it does not recover from a loss of WiFi network. 
    2)   check for nullptr / crash when STA fails... as it does cause it ? seems stable..  need to test further. 
    3) 
    4) 
    5) 
    6) 
    7) 
*/

#ifndef ESP_DEVICE_FINDER_H
#define ESP_DEVICE_FINDER_H

#include <functional>
#include <memory>

#include <ESP8266WiFi.h>
//#include <WiFiUdp.h>
#include <Udp.h>
#include <IPAddress.h>

#include <list>

#define DEFAULT_ESPDEVICE_PORT 8269

//#define DebugUDP Serial
//#define UDP_TEST_SENDER //  this sends lots of pretend values not currently implemented correctly though!

// #if defined(DebugUDP)
// //#define DebugUDPf(...) DebugUDP.printf(__VA_ARGS__)
// #define DebugUDPf(_1, ...) DebugUDP.printf_P( PSTR(_1), ##__VA_ARGS__) //  this saves around 5K RAM...

// #else
// #define DebugUDPf(...) {}
// #endif

#if defined(DEBUGDEVICEFINDER)
#define DEBUGDEVICEFINDERF(_1, ...)                                                                                                  \
  {                                                                                                                             \
    DEBUGDEVICEFINDER.printf_P(PSTR("[%-10u][%5.5s][%15.15s:L%-4u] " _1), millis(), "DEVF", __func__, __LINE__, ##__VA_ARGS__); \
  } 
#pragma message("DEBUG enabled for ESP DEVICE FINDER.")
#else
#define DEBUGDEVICEFINDERF(...){} 
#endif

class UdpContext;

class ESPdeviceFinder
{
public:

  struct UDP_item {
    ~UDP_item();
    UDP_item(IPAddress ip, std::unique_ptr<char[]>(ID) );
    IPAddress IP;
    std::unique_ptr<char[]> name;
    //std::unique_ptr<char[]> appName;
    uint32_t lastseen{0};
  };

  typedef std::list<  std::unique_ptr<UDP_item>  > UDPList;
  

  ESPdeviceFinder();
  ~ESPdeviceFinder();
  void begin(const char * host = nullptr, uint16_t port = 0);
  void end();

  void setHost(String host);
  void setAppName(String appName); 
  void setPort(uint16_t port);
  void setMulticastIP(IPAddress addr);

  void filterByAppName(bool value) { _filterByAppName = value; } 

  void loop();

  void ping()
  {
    _sendRequest(PING);
  }

  uint8_t count();
  const char * getName(uint8_t i);
  //const char * getAppName(uint8_t i); 

  IPAddress getIP(uint8_t i);
  void cacheResults(bool val); 
  void clearResults(); 

private:
  void _begin(); 

  enum UDP_REQUEST_TYPE : uint8_t { PING = 0, PONG };
  UDPList devices;
  //void _restart();
  bool _listen();
  //uint32_t _getOurIp();
  void _update();
  void _parsePacket();
  void _sendRequest(UDP_REQUEST_TYPE method);
  void _addToList(IPAddress IP, std::unique_ptr<char[]>(ID));
  void _restart();

  void _onRx();

  uint16_t _port{0};
  String _host;
  String _appName; 
  uint32_t _lastmessage{0};
  bool _cacheResults{true}; 

  bool _waiting4ping{false};
  uint32_t _checkTimeOut{0};
  uint32_t _sendPong{0};

  WiFiEventHandler _disconnectedHandler;
  WiFiEventHandler _gotIPHandler;

  UdpContext* _conn{nullptr};
  bool _initialized{false};
  IPAddress _addr;

  bool _filterByAppName{true}; 

  void _dumpMem(void * mem, size_t size); 


#ifdef UDP_TEST_SENDER
  void _test_sender();
#endif



};

#endif



#ifndef _ESP_WEBDAV_H_
#define _ESP_WEBDAV_H_

#include <ESP8266WiFi.h>
#include <SdFat.h>

#define FW_VERSION "2.1-turbo"
#define FW_BUILD __DATE__ " " __TIME__

// lowest free heap seen since boot (updated after each request + in loop)
extern uint32_t g_minFreeHeap;
// when non-zero: restart the board once millis() passes this (set after a
// web rename so the response gets out before rebooting)
extern uint32_t g_restartAt;

// Set to 1 (or build with -D DAV_DEBUG=1) for verbose per-request serial logging.
// Keep 0 for production: serial prints inside the request path slow everything down.
#ifndef DAV_DEBUG
#define DAV_DEBUG 0
#endif

#if DAV_DEBUG
	#define DBG_PRINT(...) 		{ Serial.print(__VA_ARGS__); }
	#define DBG_PRINTLN(...) 	{ Serial.println(__VA_ARGS__); }
#else
	#define DBG_PRINT(...) 		{}
	#define DBG_PRINTLN(...) 	{}
#endif

// constants for WebServer
#define CONTENT_LENGTH_UNKNOWN ((size_t) -1)
#define CONTENT_LENGTH_NOT_SET ((size_t) -2)

#define HTTP_BODY_TIMEOUT_MS	8000	// give up if the request body stalls this long
#define HTTP_HEADER_TIMEOUT_MS	1500	// Stream timeout while reading request line / headers
#define KEEPALIVE_IDLE_MS		4000	// close a keep-alive connection idle this long
#define KEEPALIVE_YIELD_MS		150		// close an idle connection early when another client waits
#define XFER_BUF_SIZE			4096	// SD<->TCP transfer buffer, must be a multiple of 512

enum ResourceType { RESOURCE_NONE, RESOURCE_FILE, RESOURCE_DIR };
enum DepthType { DEPTH_NONE, DEPTH_CHILD, DEPTH_ALL };


class ESPWebDAV	{
	friend class FormatProgressPrint;
public:
	bool init(int chipSelectPin, SPISettings spiSettings, int serverPort);
	// open the TCP port without touching the SPI bus (used when another host
	// owns the card at boot - the web UI must still come up)
	bool beginServer(int serverPort);
	bool initSD(int chipSelectPin, SPISettings spiSettings);
	// ~1 ms "is a card physically there?" probe. A full sd.begin() on an empty
	// slot blocks for SECONDS, which stalls the whole server.
	bool cardPresent(int chipSelectPin);
	bool startServer();
	bool sdHealthy();
	// connection bookkeeping - never touches the SD bus, safe to call every loop
	void maintainClient();
	// true when a full/partial request has arrived and should be served now
	bool requestPending();
	// serve one request - caller must own the SD bus
	void handleClient();
	// serve one request without any SD access (SD busy / not ready)
	void rejectClient(const char *rejectMessage);

protected:
	typedef void (ESPWebDAV::*THandlerFunction)(const char *);

	void processClient(THandlerFunction handler, const char *message);
	void handleNotFound();
	void handleReject(const char *rejectMessage);
	void handleRequest(const char *blank);
	void handleOptions(ResourceType resource);
	void handleLock(ResourceType resource);
	void handleUnlock(ResourceType resource);
	void handlePropPatch(ResourceType resource);
	void handleProp(ResourceType resource);
	void sendPropResponse(bool recursing, FatFile *curFile);
	void handleWebPage();
	void handleListJson();
	void handleStatusJson();
	void handleFormat();
	void handleSetName();
	void handleSetWifi();
	void handleSetSerial();
	void handleOtaUpdate();
	void handleGet(ResourceType resource, bool isGet);
	void handlePut(ResourceType resource);
	void handlePutChunked(ResourceType resource);
	void handleWriteError(const char *message, FatFile *wFile);
	void handleDirectoryCreate(ResourceType resource);
	void handleMove(ResourceType resource);
	void handleDelete(ResourceType resource);

	// Sections are copied from ESP8266Webserver
	const char *getMimeType(const char *path);
	String urlDecode(const String& text);
	String urlToUri(String url);
	String queryParam(const char *key);
	bool parseRequest();
	void sendHeader(const String& name, const String& value, bool first = false);
	void send(const char *code, const char *content_type, const String& content);
	void _prepareHeader(String& response, const char *code, const char *content_type, size_t contentLength);
	void sendContent(const String& content);
	void sendContentLen(const char *data, size_t size);
	void sendContent_P(PGM_P content);
	void setContentLength(size_t len);
	void sendContinueIfNeeded();
	size_t readBytesWithTimeout(uint8_t *buf, size_t toRead);
	long readChunkSize();
	void drainBody();

	// variables pertaining to current most HTTP request being serviced
	WiFiServer *server = NULL;
	SdFat sd;

	WiFiClient 	client;
	uint32_t	clientLastActive = 0;
	uint32_t	_cardSizeMB = 0;	// cached at mount so /?api=status never touches the bus
	uint8_t		_cardType = 0;
	String 		method;
	String 		uri;
	String 		queryString;
	String 		contentLengthHeader;
	String 		depthHeader;
	String 		hostHeader;
	String		destinationHeader;
	String		overwriteHeader;

	bool		keepClient = false;		// keep-alive this connection after the response
	bool		expect100 = false;		// client sent Expect: 100-continue
	bool		chunkedBody = false;	// request body uses chunked transfer encoding
	size_t		bodyRemaining = 0;		// unread request-body bytes (known length)

	String 		_responseHeaders;
	bool		_chunked = false;
	size_t		_contentLength = CONTENT_LENGTH_NOT_SET;
};

extern ESPWebDAV dav;

#endif

#include "ESPWebDAV.h"

// Sections are copied from ESP8266Webserver

// mime table kept small and in plain RAM - looked up per request only
struct MimeEntry { const char *ext; const char *mime; };
static const MimeEntry MIME_TABLE[] = {
	{ ".html", "text/html" },
	{ ".htm",  "text/html" },
	{ ".css",  "text/css" },
	{ ".txt",  "text/plain" },
	{ ".js",   "application/javascript" },
	{ ".json", "application/json" },
	{ ".png",  "image/png" },
	{ ".gif",  "image/gif" },
	{ ".jpg",  "image/jpeg" },
	{ ".ico",  "image/x-icon" },
	{ ".svg",  "image/svg+xml" },
	{ ".xml",  "text/xml" },
	{ ".pdf",  "application/pdf" },
	{ ".zip",  "application/zip" },
	{ ".gz",   "application/x-gzip" },
};

// ------------------------
const char *ESPWebDAV::getMimeType(const char *path) {
// ------------------------
	const char *dot = strrchr(path, '.');
	if(dot)	{
		for(size_t i = 0; i < sizeof(MIME_TABLE)/sizeof(MIME_TABLE[0]); i++)
			if(strcasecmp(dot, MIME_TABLE[i].ext) == 0)
				return MIME_TABLE[i].mime;
	}
	return "application/octet-stream";
}




// ------------------------
String ESPWebDAV::urlDecode(const String& text)	{
// ------------------------
	String decoded = "";
	decoded.reserve(text.length());
	char temp[] = "0x00";
	unsigned int len = text.length();
	unsigned int i = 0;
	while (i < len)	{
		char decodedChar;
		char encodedChar = text.charAt(i++);
		if ((encodedChar == '%') && (i + 1 < len))	{
			temp[2] = text.charAt(i++);
			temp[3] = text.charAt(i++);
			decodedChar = strtol(temp, NULL, 16);
		}
		else {
			if (encodedChar == '+')
				decodedChar = ' ';
			else
				decodedChar = encodedChar;  // normal ascii char
		}
		decoded += decodedChar;
	}
	return decoded;
}





// ------------------------
String ESPWebDAV::urlToUri(String url)	{
// ------------------------
	if(url.startsWith("http://"))	{
		int uriStart = url.indexOf('/', 7);
		return url.substring(uriStart);
	}
	else
		return url;
}



// ------------------------
String ESPWebDAV::queryParam(const char *key)	{
// ------------------------
	// extract & url-decode one "key=value" from the raw query string
	String k = String(key) + "=";
	int pos = 0;
	while(pos >= 0 && pos < (int) queryString.length())	{
		int amp = queryString.indexOf('&', pos);
		String seg = (amp < 0) ? queryString.substring(pos) : queryString.substring(pos, amp);
		if(seg.startsWith(k))
			return urlDecode(seg.substring(k.length()));
		pos = (amp < 0) ? -1 : amp + 1;
	}
	return String();
}



// ------------------------
void ESPWebDAV::maintainClient() {
// ------------------------
	// Non-blocking connection bookkeeping, called every loop().
	// The old code did "while(!client.available()) delay(1);" with no timeout
	// and no connected() check - one idle probe connection from Windows froze
	// the whole board until power cycle.
	if(!server)
		return;

	if(client.connected())	{
		if(client.available())
			return;			// a request is waiting - keep the connection

		uint32_t idle = millis() - clientLastActive;
		if(idle > KEEPALIVE_IDLE_MS || (server->hasClient() && idle > KEEPALIVE_YIELD_MS))
			client.stop();	// recycle: idle too long, or someone else needs us
		else
			return;
	}

	// slot is free - adopt a waiting client if there is one
	if(server->hasClient())	{
		client = server->accept();
		client.setNoDelay(true);
		client.setTimeout(HTTP_HEADER_TIMEOUT_MS);
		clientLastActive = millis();
	}
}



// ------------------------
bool ESPWebDAV::requestPending() {
// ------------------------
	return client.available() > 0;
}



// ------------------------
void ESPWebDAV::handleClient() {
// ------------------------
	processClient(&ESPWebDAV::handleRequest, "");
}



// ------------------------
void ESPWebDAV::rejectClient(const char *rejectMessage) {
// ------------------------
	processClient(&ESPWebDAV::handleReject, rejectMessage);
}



// ------------------------
void ESPWebDAV::processClient(THandlerFunction handler, const char *message) {
// ------------------------
	if(!client.available())
		return;

	// reset all per-request variables
	_chunked = false;
	_responseHeaders = String();
	_contentLength = CONTENT_LENGTH_NOT_SET;
	method = String();
	uri = String();
	queryString = String();
	contentLengthHeader = String();
	depthHeader = String();
	hostHeader = String();
	destinationHeader = String();
	overwriteHeader = String();
	keepClient = false;
	expect100 = false;
	chunkedBody = false;
	bodyRemaining = 0;

	client.setTimeout(HTTP_HEADER_TIMEOUT_MS);

	// extract uri, headers etc
	if(parseRequest())
		// invoke the handler
		(this->*handler)(message);
	else
		keepClient = false;

	// finalize the response
	if(_chunked)
		sendContent("");

	// eat whatever request-body bytes the handler did not consume, so the
	// next request on this connection starts at a clean boundary
	drainBody();

	clientLastActive = millis();

	// heap low-water mark for the status panel / hang diagnostics
	uint32_t h = ESP.getFreeHeap();
	if(h < g_minFreeHeap) g_minFreeHeap = h;

	if(!keepClient)
		client.stop();
}




// ------------------------
bool ESPWebDAV::parseRequest() {
// ------------------------
	// Read the first line of HTTP request
	String req = client.readStringUntil('\r');
	client.readStringUntil('\n');

	// tolerate a stray CRLF left over from the previous request
	if(req.length() == 0)	{
		req = client.readStringUntil('\r');
		client.readStringUntil('\n');
	}

	// First line of HTTP request looks like "GET /path HTTP/1.1"
	// Retrieve the "/path" part by finding the spaces
	int addr_start = req.indexOf(' ');
	int addr_end = req.indexOf(' ', addr_start + 1);
	if (addr_start == -1 || addr_end == -1) {
		return false;
	}

	method = req.substring(0, addr_start);

	// split "?query" off before decoding the path
	String rawUri = req.substring(addr_start + 1, addr_end);
	int qPos = rawUri.indexOf('?');
	if(qPos >= 0)	{
		queryString = rawUri.substring(qPos + 1);
		rawUri.remove(qPos);
	}
	uri = urlDecode(rawUri);

	// HTTP/1.1 defaults to keep-alive
	keepClient = (req.indexOf("HTTP/1.1", addr_end) >= 0);

	// parse and finish all headers
	String headerName;
	String headerValue;

	while(1) {
		req = client.readStringUntil('\r');
		client.readStringUntil('\n');
		if(req == "")
			// no more headers
			break;

		int headerDiv = req.indexOf(':');
		if (headerDiv == -1)
			break;

		headerName = req.substring(0, headerDiv);
		headerValue = req.substring(headerDiv + 1);
		headerValue.trim();

		if(headerName.equalsIgnoreCase("Host"))
			hostHeader = headerValue;
		else if(headerName.equalsIgnoreCase("Depth"))
			depthHeader = headerValue;
		else if(headerName.equalsIgnoreCase("Content-Length"))
			contentLengthHeader = headerValue;
		else if(headerName.equalsIgnoreCase("Destination"))
			destinationHeader = headerValue;
		else if(headerName.equalsIgnoreCase("Overwrite"))
			overwriteHeader = headerValue;
		else if(headerName.equalsIgnoreCase("Connection"))	{
			if(headerValue.equalsIgnoreCase("close"))
				keepClient = false;
			else if(headerValue.indexOf("eep-") >= 0)	// Keep-Alive / keep-alive
				keepClient = true;
		}
		else if(headerName.equalsIgnoreCase("Expect"))	{
			if(headerValue.indexOf("100-continue") >= 0)
				expect100 = true;
		}
		else if(headerName.equalsIgnoreCase("Transfer-Encoding"))	{
			if(headerValue.indexOf("hunked") >= 0)
				chunkedBody = true;
		}
	}

	bodyRemaining = chunkedBody ? 0 : strtoul(contentLengthHeader.c_str(), NULL, 10);
	return true;
}




// ------------------------
void ESPWebDAV::sendHeader(const String& name, const String& value, bool first) {
// ------------------------
	String headerLine = name + ": " + value + "\r\n";

	if (first)
		_responseHeaders = headerLine + _responseHeaders;
	else
		_responseHeaders += headerLine;
}



// ------------------------
void ESPWebDAV::send(const char *code, const char *content_type, const String& content) {
// ------------------------
	String header;
	_prepareHeader(header, code, content_type, content.length());

	client.write(header.c_str(), header.length());
	if(content.length())
		sendContent(content);
}



// ------------------------
void ESPWebDAV::_prepareHeader(String& response, const char *code, const char *content_type, size_t contentLength) {
// ------------------------
	response = "HTTP/1.1 ";
	response += code;
	response += "\r\n";

	if(content_type)
		sendHeader("Content-Type", content_type, true);

	if(_contentLength == CONTENT_LENGTH_NOT_SET)
		sendHeader("Content-Length", String(contentLength));
	else if(_contentLength != CONTENT_LENGTH_UNKNOWN)
		sendHeader("Content-Length", String(_contentLength));
	else {	// CONTENT_LENGTH_UNKNOWN
		_chunked = true;
		sendHeader("Accept-Ranges","none");
		sendHeader("Transfer-Encoding","chunked");
	}
	sendHeader("Connection", keepClient ? "keep-alive" : "close");

	response += _responseHeaders;
	response += "\r\n";
}



// ------------------------
void ESPWebDAV::sendContentLen(const char *data, size_t size) {
// ------------------------
	if(_chunked) {
		char chunkHead[12];
		int l = snprintf(chunkHead, sizeof(chunkHead), "%x\r\n", (unsigned) size);
		client.write(chunkHead, l);
	}

	if(size)
		client.write(data, size);

	if(_chunked) {
		client.write("\r\n", 2);
		if (size == 0)
			_chunked = false;
	}
}



// ------------------------
void ESPWebDAV::sendContent(const String& content) {
// ------------------------
	sendContentLen(content.c_str(), content.length());
}



// ------------------------
void ESPWebDAV::sendContent_P(PGM_P content) {
// ------------------------
	size_t size = strlen_P(content);

	if(_chunked) {
		char chunkHead[12];
		int l = snprintf(chunkHead, sizeof(chunkHead), "%x\r\n", (unsigned) size);
		client.write(chunkHead, l);
	}

	if(size)
		client.write_P(content, size);

	if(_chunked) {
		client.write("\r\n", 2);
		if (size == 0)
			_chunked = false;
	}
}



// ------------------------
void ESPWebDAV::setContentLength(size_t len)	{
// ------------------------
	_contentLength = len;
}



// ------------------------
void ESPWebDAV::sendContinueIfNeeded()	{
// ------------------------
	// client is holding the body back until we bless it
	if(!expect100)
		return;
	client.write("HTTP/1.1 100 Continue\r\n\r\n", 25);
	expect100 = false;
}



// ------------------------
size_t ESPWebDAV::readBytesWithTimeout(uint8_t *buf, size_t toRead) {
// ------------------------
	// Fills the buffer completely unless the stream stalls for
	// HTTP_BODY_TIMEOUT_MS or the client disconnects with nothing buffered.
	// (The old implementation returned whatever a single read() gave it -
	// callers that assumed full blocks then corrupted uploads.)
	size_t got = 0;
	uint32_t deadline = millis() + HTTP_BODY_TIMEOUT_MS;

	while(got < toRead && (int32_t)(deadline - millis()) > 0)	{
		size_t avail = client.available();
		if(!avail)	{
			if(!client.connected())
				break;
			delay(1);
			continue;
		}

		size_t chunk = toRead - got;
		if(chunk > avail)
			chunk = avail;
		int r = client.read(buf + got, chunk);
		if(r <= 0)	{
			if(!client.connected())
				break;
			delay(1);
			continue;
		}

		got += r;
		deadline = millis() + HTTP_BODY_TIMEOUT_MS;	// progress resets the clock
	}

	return got;
}



// ------------------------
long ESPWebDAV::readChunkSize()	{
// ------------------------
	// reads a "hex[;ext]\r\n" chunk-size line; skips blank lines
	char line[16];
	uint8_t idx = 0;
	uint32_t deadline = millis() + HTTP_BODY_TIMEOUT_MS;

	while((int32_t)(deadline - millis()) > 0)	{
		int c = client.read();
		if(c < 0)	{
			if(!client.connected())
				return -1;
			delay(1);
			continue;
		}
		if(c == '\n')	{
			if(idx == 0)
				continue;		// blank line between chunks
			line[idx] = 0;
			return strtol(line, NULL, 16);
		}
		if(c != '\r' && idx < sizeof(line) - 1)
			line[idx++] = (char) c;
	}
	return -1;
}



// ------------------------
void ESPWebDAV::drainBody() {
// ------------------------
	if(chunkedBody)	{
		// handler did not finish a chunked body - can't resync, drop the link
		keepClient = false;
		return;
	}

	if(bodyRemaining == 0)
		return;

	// a big leftover body isn't worth reading through - just close
	if(bodyRemaining > 16384)	{
		keepClient = false;
		bodyRemaining = 0;
		return;
	}

	uint32_t deadline = millis() + 1000;
	uint8_t sink[64];
	while(bodyRemaining > 0 && (int32_t)(deadline - millis()) > 0)	{
		size_t chunk = bodyRemaining > sizeof(sink) ? sizeof(sink) : bodyRemaining;
		int r = client.read(sink, chunk);
		if(r > 0)	{
			bodyRemaining -= r;
			deadline = millis() + 1000;
		}
		else	{
			if(!client.connected())
				return;
			delay(1);
		}
	}

	if(bodyRemaining > 0)	{
		keepClient = false;
		bodyRemaining = 0;
	}
}

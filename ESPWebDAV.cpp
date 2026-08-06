// WebDAV server using ESP8266 and SD card filesystem
// Targeting Windows 7+ Explorer WebDav
//
// "turbo" rework: keep-alive connections, sector-aligned 4KB transfers,
// no heap churn in the hot paths, robust timeouts everywhere.

#include <ESP8266WiFi.h>
#include <SPI.h>
#include <SdFat.h>
#include <time.h>
#include "ESPWebDAV.h"
#include "webui.h"
#include "config.h"
#include "sdControl.h"
#include "pins.h"
#include <Updater.h>

uint32_t g_minFreeHeap = 0xFFFFFFFF;
uint32_t g_restartAt = 0;

// Print sink for sd.wipe(): forwards each progress dot (one per 256 sector
// writes) into the chunked HTTP response so the browser can draw a real
// progress ring, and feeds the watchdog at the same time.
class FormatProgressPrint : public Print {
public:
	ESPWebDAV *dav;
	FormatProgressPrint(ESPWebDAV *d) : dav(d) {}
	size_t write(uint8_t c) override {
		if(c == '.')
			dav->sendContentLen(".", 1);
		yield();
		return 1;
	}
};

// define cal constants
const char *months[]  = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
const char *wdays[]  = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

// Shared static buffers - single threaded server, so these are safe.
// Living here (not on the 4KB task stack!) also fixes the stack overflows
// the old code could hit in LOCK/GET.
static uint8_t s_buf[XFER_BUF_SIZE];		// SD <-> TCP payload buffer
static char s_scratch[2048];				// XML/HTML fragment assembly
static char s_href[640];					// raw href/path building
static char s_hrefEsc[960];					// escaped href

// ------------------------
// tiny escape helpers
// ------------------------
static size_t xmlEscapeInto(char *dst, size_t cap, const char *src)	{
	size_t o = 0;
	for(; *src && o + 7 < cap; src++)	{
		switch(*src)	{
			case '&': memcpy(dst + o, "&amp;", 5); o += 5; break;
			case '<': memcpy(dst + o, "&lt;", 4); o += 4; break;
			case '>': memcpy(dst + o, "&gt;", 4); o += 4; break;
			case '"': memcpy(dst + o, "&quot;", 6); o += 6; break;
			default: dst[o++] = *src; break;
		}
	}
	dst[o] = 0;
	return o;
}

static size_t jsonEscapeInto(char *dst, size_t cap, const char *src)	{
	size_t o = 0;
	for(; *src && o + 3 < cap; src++)	{
		unsigned char c = (unsigned char) *src;
		if(c == '"' || c == '\\')	{
			dst[o++] = '\\';
			dst[o++] = c;
		}
		else if(c < 0x20)
			dst[o++] = ' ';
		else
			dst[o++] = c;
	}
	dst[o] = 0;
	return o;
}



// ------------------------
bool ESPWebDAV::init(int chipSelectPin, SPISettings spiSettings, int serverPort) {
// ------------------------
	// start the wifi server
	if(!server)	{
		server = new WiFiServer(serverPort);
		server->begin();
		server->setNoDelay(true);
	}

	// initialize the SD card
	return initSD(chipSelectPin, spiSettings);
}

// ------------------------
bool ESPWebDAV::initSD(int chipSelectPin, SPISettings spiSettings) {
	// (re)initialize the SD card - also used for hot re-mount after errors
	bool ok = sd.begin(chipSelectPin, spiSettings);
	if(ok)	{
		// cache card info now (bus is ours) so status queries never need it
		_cardSizeMB = sd.card()->cardSize() >> 11;	// 512B blocks -> MB
		_cardType = sd.card()->type();
	}
	return ok;
}

// ------------------------
bool ESPWebDAV::startServer() {
// ------------------------
	if(server)
		server->begin();
	return true;
}

// ------------------------
bool ESPWebDAV::sdHealthy() {
// ------------------------
	// SdFat latches a low-level error code on card failures (removed, glitched...)
	return sd.card()->errorCode() == 0;
}


// ------------------------
void ESPWebDAV::handleNotFound() {
// ------------------------
	String message = "Not found\n";
	message += "URI: ";
	message += uri;
	message += " Method: ";
	message += method;
	message += "\n";

	sendHeader("Allow", "OPTIONS,MKCOL,POST,PUT");
	send("404 Not Found", "text/plain", message);
	DBG_PRINTLN("404 Not Found");
}



// ------------------------
void ESPWebDAV::handleReject(const char *rejectMessage)	{
// ------------------------
	DBG_PRINT("Rejecting request: "); DBG_PRINTLN(rejectMessage);

	// handle options
	if(method.equals("OPTIONS"))
		return handleOptions(RESOURCE_NONE);

	// browser traffic should keep working while the SD is busy or absent:
	// status/rename/OTA touch no SD at all, and navigations get the app
	// shell (which shows the "SD busy" / "no card" banner by itself)
	if(method.equals("GET"))	{
		if(queryString.indexOf("api=status") >= 0)
			return handleStatusJson();
		if(queryString.length() == 0)	{
			int lastSlash = uri.lastIndexOf('/');
			if(uri.endsWith("/") || uri.indexOf('.', lastSlash) < 0)
				return handleWebPage();
		}
	}
	if(method.equals("POST"))	{
		if(queryString.indexOf("api=name") >= 0)
			return handleSetName();
		if(queryString.indexOf("api=ota") >= 0)
			return handleOtaUpdate();
		if(queryString.indexOf("api=wifi") >= 0)
			return handleSetWifi();
		if(queryString.indexOf("api=serial") >= 0)
			return handleSetSerial();
	}

	// handle properties - fake a single-file listing so Explorer shows the reason
	if(method.equals("PROPFIND"))	{
		sendHeader("Allow", "PROPFIND,OPTIONS,DELETE,COPY,MOVE");
		setContentLength(CONTENT_LENGTH_UNKNOWN);
		send("207 Multi-Status", "application/xml;charset=utf-8", "");
		sendContent_P(PSTR("<?xml version=\"1.0\" encoding=\"utf-8\"?><D:multistatus xmlns:D=\"DAV:\"><D:response><D:href>/</D:href><D:propstat><D:status>HTTP/1.1 200 OK</D:status><D:prop><D:getlastmodified>Fri, 30 Nov 1979 00:00:00 GMT</D:getlastmodified><D:getetag>\"3333333333333333333333333333333333333333\"</D:getetag><D:resourcetype><D:collection/></D:resourcetype></D:prop></D:propstat></D:response>"));

		if(depthHeader.equals("1"))	{
			sendContent_P(PSTR("<D:response><D:href>/"));
			sendContentLen(rejectMessage, strlen(rejectMessage));
			sendContent_P(PSTR("</D:href><D:propstat><D:status>HTTP/1.1 200 OK</D:status><D:prop><D:getlastmodified>Fri, 01 Apr 2016 16:07:40 GMT</D:getlastmodified><D:getetag>\"2222222222222222222222222222222222222222\"</D:getetag><D:resourcetype/><D:getcontentlength>0</D:getcontentlength><D:getcontenttype>application/octet-stream</D:getcontenttype></D:prop></D:propstat></D:response>"));
		}

		sendContent_P(PSTR("</D:multistatus>"));
		return;
	}

	// anything else: tell the client we are temporarily busy so it retries,
	// instead of a misleading 404
	sendHeader("Retry-After", "5");
	send("503 Service Unavailable", "text/plain", rejectMessage);
}




// ------------------------
void ESPWebDAV::handleRequest(const char *blank)	{
// ------------------------
	(void) blank;
	ResourceType resource = RESOURCE_NONE;

	// does uri refer to a file or directory or a null?
	FatFile tFile;
	if(tFile.open(sd.vwd(), uri.c_str(), O_READ))	{
		resource = tFile.isDir() ? RESOURCE_DIR : RESOURCE_FILE;
		tFile.close();
	}

	DBG_PRINT("\r\nm: "); DBG_PRINT(method);
	DBG_PRINT(" r: "); DBG_PRINT(resource);
	DBG_PRINT(" u: "); DBG_PRINTLN(uri);

	// add header that gets sent everytime
	sendHeader("DAV", "1,2");

	// handle properties
	if(method.equals("PROPFIND"))
		return handleProp(resource);

	if(method.equals("GET"))	{
		// JSON api for the neon web UI
		if(queryString.indexOf("api=status") >= 0)
			return handleStatusJson();
		if(queryString.indexOf("api=list") >= 0)	{
			if(resource == RESOURCE_DIR)
				return handleListJson();
			return handleNotFound();
		}
		if(resource == RESOURCE_DIR)
			return handleWebPage();
		return handleGet(resource, true);
	}

	if(method.equals("HEAD"))
		return handleGet(resource, false);

	// handle options
	if(method.equals("OPTIONS"))
		return handleOptions(resource);

	// quick-format from the web UI: POST /?api=format&confirm=FORMAT
	if(method.equals("POST") && queryString.indexOf("api=format") >= 0)
		return handleFormat();

	// board rename from the web UI: POST /?api=name&v=NEW-NAME
	if(method.equals("POST") && queryString.indexOf("api=name") >= 0)
		return handleSetName();

	// firmware update from the web UI: POST /?api=ota (raw .bin body)
	if(method.equals("POST") && queryString.indexOf("api=ota") >= 0)
		return handleOtaUpdate();

	// web settings: wifi credentials / serial baud
	if(method.equals("POST") && queryString.indexOf("api=wifi") >= 0)
		return handleSetWifi();
	if(method.equals("POST") && queryString.indexOf("api=serial") >= 0)
		return handleSetSerial();

	// handle file create/uploads
	if(method.equals("PUT"))
		return handlePut(resource);

	// handle file locks
	if(method.equals("LOCK"))
		return handleLock(resource);

	if(method.equals("UNLOCK"))
		return handleUnlock(resource);

	if(method.equals("PROPPATCH"))
		return handlePropPatch(resource);

	// directory creation
	if(method.equals("MKCOL"))
		return handleDirectoryCreate(resource);

	// move a file or directory
	if(method.equals("MOVE"))
		return handleMove(resource);

	// delete a file or directory
	if(method.equals("DELETE"))
		return handleDelete(resource);

	// if reached here, means its a 404
	handleNotFound();
}



// ------------------------
void ESPWebDAV::handleOptions(ResourceType resource)	{
// ------------------------
	(void) resource;
	DBG_PRINTLN("Processing OPTION");
	sendHeader("Allow", "OPTIONS,PROPFIND,PROPPATCH,GET,HEAD,DELETE,PUT,COPY,MOVE,MKCOL,LOCK,UNLOCK");
	sendHeader("MS-Author-Via", "DAV");
	send("200 OK", NULL, "");
}



// ------------------------
void ESPWebDAV::handleLock(ResourceType resource)	{
// ------------------------
	(void) resource;
	DBG_PRINTLN("Processing LOCK");

	// locks are simulated: grant one whatever the resource state is.
	// (Some clients LOCK before the first PUT of a new file.)
	sendHeader("Allow", "PROPPATCH,PROPFIND,OPTIONS,DELETE,UNLOCK,COPY,LOCK,MOVE,HEAD,POST,PUT,GET");
	sendHeader("Lock-Token", "urn:uuid:26e57cb3-834d-191a-00de-000042bdecf9");

	// pull the owner href out of the body if one was sent
	char owner[160];
	owner[0] = 0;
	if(bodyRemaining > 0)	{
		sendContinueIfNeeded();
		size_t want = bodyRemaining < (size_t)(XFER_BUF_SIZE - 1) ? bodyRemaining : (size_t)(XFER_BUF_SIZE - 1);
		size_t got = readBytesWithTimeout(s_buf, want);
		bodyRemaining -= got;
		s_buf[got] = 0;		// bounded: got <= XFER_BUF_SIZE-1 (old code smashed the stack here)

		// matches <D:href>, <a:href>, <href>...
		const char *h = strstr((const char *) s_buf, "href>");
		if(h)	{
			h += 5;
			const char *e = strstr(h, "</");
			if(e && (size_t)(e - h) < sizeof(owner))	{
				memcpy(owner, h, e - h);
				owner[e - h] = 0;
			}
		}
	}

	int n = snprintf_P(s_scratch, sizeof(s_scratch),
		PSTR("<?xml version=\"1.0\" encoding=\"utf-8\"?><D:prop xmlns:D=\"DAV:\"><D:lockdiscovery><D:activelock><D:locktype><write/></D:locktype><D:lockscope><exclusive/></D:lockscope><D:locktoken><D:href>urn:uuid:26e57cb3-834d-191a-00de-000042bdecf9</D:href></D:locktoken><D:lockroot><D:href>%s</D:href></D:lockroot><D:depth>infinity</D:depth><D:owner><a:href xmlns:a=\"DAV:\">%s</a:href></D:owner><D:timeout>Second-3600</D:timeout></D:activelock></D:lockdiscovery></D:prop>"),
		uri.c_str(), owner);
	if(n < 0) n = 0;
	if((size_t)n >= sizeof(s_scratch)) n = sizeof(s_scratch) - 1;

	send("200 OK", "application/xml;charset=utf-8", String(s_scratch));
}



// ------------------------
void ESPWebDAV::handleUnlock(ResourceType resource)	{
// ------------------------
	(void) resource;
	DBG_PRINTLN("Processing UNLOCK");
	sendHeader("Allow", "PROPPATCH,PROPFIND,OPTIONS,DELETE,UNLOCK,COPY,LOCK,MOVE,HEAD,POST,PUT,GET");
	sendHeader("Lock-Token", "urn:uuid:26e57cb3-834d-191a-00de-000042bdecf9");
	send("204 No Content", NULL, "");
}



// ------------------------
void ESPWebDAV::handlePropPatch(ResourceType resource)	{
// ------------------------
	DBG_PRINTLN("PROPPATCH forwarding to PROPFIND");
	handleProp(resource);
}



// ------------------------
void ESPWebDAV::handleProp(ResourceType resource)	{
// ------------------------
	DBG_PRINTLN("Processing PROPFIND");
	// check depth header
	DepthType depth = DEPTH_NONE;
	if(depthHeader.equals("1"))
		depth = DEPTH_CHILD;
	else if(depthHeader.equals("infinity"))
		depth = DEPTH_ALL;

	DBG_PRINT("Depth: "); DBG_PRINTLN(depth);

	// does URI refer to an existing resource
	if(resource == RESOURCE_NONE)
		return handleNotFound();

	if(resource == RESOURCE_FILE)
		sendHeader("Allow", "PROPFIND,OPTIONS,DELETE,COPY,MOVE,HEAD,POST,PUT,GET");
	else
		sendHeader("Allow", "PROPFIND,OPTIONS,DELETE,COPY,MOVE");

	setContentLength(CONTENT_LENGTH_UNKNOWN);
	send("207 Multi-Status", "application/xml;charset=utf-8", "");
	sendContent_P(PSTR("<?xml version=\"1.0\" encoding=\"utf-8\"?>"));
	sendContent_P(PSTR("<D:multistatus xmlns:D=\"DAV:\">"));

	// open this resource
	SdFile baseFile;
	baseFile.open(uri.c_str(), O_READ);
	sendPropResponse(false, &baseFile);

	if((resource == RESOURCE_DIR) && (depth == DEPTH_CHILD))	{
		// append children information to message
		SdFile childFile;
		while(childFile.openNext(&baseFile, O_READ)) {
			yield();
			sendPropResponse(true, &childFile);
			childFile.close();
		}
	}

	baseFile.close();
	sendContent_P(PSTR("</D:multistatus>"));
}



// ------------------------
void ESPWebDAV::sendPropResponse(bool recursing, FatFile *curFile)	{
// ------------------------
	char nameBuf[256];
	curFile->getName(nameBuf, sizeof(nameBuf));

	// build the full path of this resource
	if(recursing)
		snprintf(s_href, sizeof(s_href), "%s%s%s", uri.c_str(), uri.endsWith("/") ? "" : "/", nameBuf);
	else
		strlcpy(s_href, uri.c_str(), sizeof(s_href));
	xmlEscapeInto(s_hrefEsc, sizeof(s_hrefEsc), s_href);

	// get file modified time
	dir_t dir;
	curFile->dirEntry(&dir);

	// convert to required format
	tm tmStr;
	tmStr.tm_hour = FAT_HOUR(dir.lastWriteTime);
	tmStr.tm_min = FAT_MINUTE(dir.lastWriteTime);
	tmStr.tm_sec = FAT_SECOND(dir.lastWriteTime);
	tmStr.tm_year = FAT_YEAR(dir.lastWriteDate) - 1900;
	tmStr.tm_mon = FAT_MONTH(dir.lastWriteDate) - 1;
	tmStr.tm_mday = FAT_DAY(dir.lastWriteDate);
	tmStr.tm_isdst = 0;
	time_t t2t = mktime(&tmStr);
	tm *gTm = gmtime(&t2t);

	// Tue, 13 Oct 2015 17:07:35 GMT
	char timeStr[36];
	snprintf(timeStr, sizeof(timeStr), "%s, %02d %s %04d %02d:%02d:%02d GMT",
		wdays[gTm->tm_wday], gTm->tm_mday, months[gTm->tm_mon], gTm->tm_year + 1900,
		gTm->tm_hour, gTm->tm_min, gTm->tm_sec);

	// cheap but stable etag: size + FAT timestamp (the old sha1-per-file burned
	// CPU and heap on every directory refresh)
	unsigned long etagTime = ((unsigned long) dir.lastWriteDate << 16) | dir.lastWriteTime;

	// assemble the whole <D:response> in one buffer -> one chunked write per entry
	int n = snprintf_P(s_scratch, sizeof(s_scratch),
		PSTR("<D:response><D:href>%s</D:href><D:propstat><D:status>HTTP/1.1 200 OK</D:status><D:prop><D:getlastmodified>%s</D:getlastmodified><D:getetag>\"%lx-%lx\"</D:getetag>"),
		s_hrefEsc, timeStr, (unsigned long) curFile->fileSize(), etagTime);

	if(curFile->isDir())
		n += snprintf_P(s_scratch + n, sizeof(s_scratch) - n, PSTR("<D:resourcetype><D:collection/></D:resourcetype>"));
	else
		n += snprintf_P(s_scratch + n, sizeof(s_scratch) - n,
			PSTR("<D:resourcetype/><D:getcontentlength>%lu</D:getcontentlength><D:getcontenttype>%s</D:getcontenttype>"),
			(unsigned long) curFile->fileSize(), getMimeType(s_href));

	n += snprintf_P(s_scratch + n, sizeof(s_scratch) - n, PSTR("</D:prop></D:propstat></D:response>"));
	if(n < 0) n = 0;
	if((size_t)n >= sizeof(s_scratch)) n = sizeof(s_scratch) - 1;

	sendContentLen(s_scratch, n);
}



// ------------------------
void ESPWebDAV::handleWebPage()	{
// ------------------------
	// neon single-page file manager, streamed straight from flash
	DBG_PRINTLN("Serving web UI");
	size_t len = strlen_P(WEBUI_HTML);
	sendHeader("Cache-Control", "no-cache");
	setContentLength(len);
	send("200 OK", "text/html;charset=utf-8", "");
	client.write_P(WEBUI_HTML, len);
}



// ------------------------
void ESPWebDAV::handleListJson()	{
// ------------------------
	DBG_PRINTLN("Processing api=list");

	SdFile dirFile;
	if(!dirFile.open(uri.c_str(), O_READ))
		return handleNotFound();

	setContentLength(CONTENT_LENGTH_UNKNOWN);
	sendHeader("Cache-Control", "no-cache");
	send("200 OK", "application/json", "");

	jsonEscapeInto(s_hrefEsc, sizeof(s_hrefEsc), uri.c_str());
	int n = snprintf_P(s_scratch, sizeof(s_scratch), PSTR("{\"path\":\"%s\",\"items\":["), s_hrefEsc);
	sendContentLen(s_scratch, n);

	SdFile entry;
	char nameBuf[256];
	char nameEsc[560];
	bool first = true;
	while(entry.openNext(&dirFile, O_READ))	{
		yield();
		entry.getName(nameBuf, sizeof(nameBuf));
		jsonEscapeInto(nameEsc, sizeof(nameEsc), nameBuf);

		dir_t d;
		entry.dirEntry(&d);

		n = snprintf_P(s_scratch, sizeof(s_scratch),
			PSTR("%s{\"n\":\"%s\",\"d\":%d,\"s\":%lu,\"t\":\"%04u-%02u-%02u %02u:%02u\"}"),
			first ? "" : ",", nameEsc, entry.isDir() ? 1 : 0, (unsigned long) entry.fileSize(),
			FAT_YEAR(d.lastWriteDate), FAT_MONTH(d.lastWriteDate), FAT_DAY(d.lastWriteDate),
			FAT_HOUR(d.lastWriteTime), FAT_MINUTE(d.lastWriteTime));
		if(n < 0) n = 0;
		if((size_t)n >= sizeof(s_scratch)) n = sizeof(s_scratch) - 1;
		sendContentLen(s_scratch, n);
		first = false;
		entry.close();
	}
	dirFile.close();

	sendContent_P(PSTR("]}"));
}



// ------------------------
void ESPWebDAV::handleStatusJson()	{
// ------------------------
	// board vitals - deliberately touches neither the SD card nor the bus,
	// so it works even while the USB reader / printer owns the card
	uint32_t heap = ESP.getFreeHeap();
	if(heap < g_minFreeHeap) g_minFreeHeap = heap;
	uint32_t minHeap = (g_minFreeHeap == 0xFFFFFFFF) ? heap : g_minFreeHeap;

	const char *ct;
	switch(_cardType)	{
		case 1:  ct = "SD1"; break;
		case 2:  ct = "SD2"; break;
		case 3:  ct = "SDHC/XC"; break;
		default: ct = "-"; break;
	}

	char ssidEsc[72];
	jsonEscapeInto(ssidEsc, sizeof(ssidEsc), config.ssid());

	// flash figures never change until an OTA (which reboots) - read once
	static uint32_t s_sketch = 0, s_flashFree = 0, s_flashSize = 0;
	if(s_flashSize == 0)	{
		s_sketch = ESP.getSketchSize();
		s_flashFree = ESP.getFreeSketchSpace();
		s_flashSize = ESP.getFlashChipRealSize();
	}

	int n = snprintf_P(s_scratch, sizeof(s_scratch),
		PSTR("{\"fw\":\"%s\",\"build\":\"%s\",\"name\":\"%s\",\"ssid\":\"%s\",\"baud\":%lu,\"up\":%lu,\"heap\":%lu,\"minheap\":%lu,\"maxblk\":%lu,\"frag\":%u,\"sketch\":%lu,\"flashfree\":%lu,\"flashsize\":%lu,\"rssi\":%d,\"cpu\":%u,\"sd\":%u,\"busy\":%u,\"cardmb\":%lu,\"cardtype\":\"%s\"}"),
		FW_VERSION, FW_BUILD, config.hostname(), ssidEsc, (unsigned long) config.baud(),
		(unsigned long)(millis() / 1000UL),
		(unsigned long) heap, (unsigned long) minHeap,
		(unsigned long) ESP.getMaxFreeBlockSize(), (unsigned) ESP.getHeapFragmentation(),
		(unsigned long) s_sketch, (unsigned long) s_flashFree, (unsigned long) s_flashSize,
		(int) WiFi.RSSI(), (unsigned) ESP.getCpuFreqMHz(),
		(_cardSizeMB > 0 && sdHealthy()) ? 1 : 0,
		sdcontrol.canWeTakeBus() ? 0 : 1,
		(unsigned long) _cardSizeMB, ct);
	if(n < 0) n = 0;
	if((size_t)n >= sizeof(s_scratch)) n = sizeof(s_scratch) - 1;

	sendHeader("Cache-Control", "no-cache");
	send("200 OK", "application/json", String(s_scratch));
}



// ------------------------
void ESPWebDAV::handleFormat()	{
// ------------------------
	// Quick format: zeroes both FATs + the root directory of the existing
	// FAT16/32 volume, then remounts. All file data becomes unreachable.
	// (A brand-new or corrupted card still needs a full format on a PC.)
	if(queryString.indexOf("confirm=FORMAT") < 0)	{
		keepClient = false;
		return send("400 Bad Request", "application/json", "{\"ok\":0,\"err\":\"confirmation required\"}");
	}

	DBG_PRINTLN("Quick-formatting SD card");
	uint32_t t0 = millis();

	// stream progress: "T:<expected dots>" first, then one '.' per 256
	// sectors wiped, then "OK:<ms>" or "FAIL"
	setContentLength(CONTENT_LENGTH_UNKNOWN);
	sendHeader("Cache-Control", "no-cache");
	send("200 OK", "text/plain", "");

	uint32_t expectedDots = (2UL * sd.vol()->blocksPerFat()) / 256UL + 1;
	int n = snprintf_P(s_scratch, sizeof(s_scratch), PSTR("T:%lu\n"), (unsigned long) expectedDots);
	sendContentLen(s_scratch, n);

	FormatProgressPrint sink(this);
	bool ok = sd.wipe(&sink);

	// wipe() invalidates the mounted volume - remount and refresh card cache
	if(ok)
		ok = initSD(SD_CS, SPI_FULL_SPEED);

	if(!ok)	{
		sendContent_P(PSTR("\nFAIL\n"));
		keepClient = false;
		return;
	}

	n = snprintf_P(s_scratch, sizeof(s_scratch), PSTR("\nOK:%lu\n"), (unsigned long)(millis() - t0));
	sendContentLen(s_scratch, n);
}



// ------------------------
void ESPWebDAV::handleSetName()	{
// ------------------------
	// persist a new board name (hostname + mDNS + OTA identity), then
	// restart so DHCP/mDNS re-register cleanly under the new name
	int vi = queryString.indexOf("v=");
	if(vi < 0)	{
		keepClient = false;
		return send("400 Bad Request", "application/json", "{\"ok\":0,\"err\":\"missing name\"}");
	}

	String v = queryString.substring(vi + 2);
	int amp = v.indexOf('&');
	if(amp >= 0) v.remove(amp);

	char nm[HOSTNAME_LEN];
	size_t o = 0;
	for(size_t i = 0; i < v.length() && o < sizeof(nm) - 1; i++)	{
		char c = v[i];
		if(isalnum((unsigned char) c) || c == '-')
			nm[o++] = c;
	}
	nm[o] = 0;

	if(o == 0)	{
		keepClient = false;
		return send("400 Bad Request", "application/json", "{\"ok\":0,\"err\":\"invalid name\"}");
	}

	config.setHostname(nm);

	snprintf_P(s_scratch, sizeof(s_scratch), PSTR("{\"ok\":1,\"name\":\"%s\",\"restart\":1}"), nm);
	sendHeader("Cache-Control", "no-cache");
	send("200 OK", "application/json", String(s_scratch));

	g_restartAt = millis() + 900;	// give the response time to flush
}



// ------------------------
void ESPWebDAV::handleSetWifi()	{
// ------------------------
	// web equivalent of serial M50+M51+M52: save credentials, restart,
	// board reconnects with the new network on boot
	String ssid = queryParam("ssid");
	String pass = queryParam("pass");

	if(ssid.length() == 0 || ssid.length() > 31 || pass.length() > 63)	{
		keepClient = false;
		return send("400 Bad Request", "application/json", "{\"ok\":0,\"err\":\"bad ssid/pass\"}");
	}

	config.save(ssid.c_str(), pass.c_str());

	sendHeader("Cache-Control", "no-cache");
	send("200 OK", "application/json", "{\"ok\":1,\"restart\":1}");
	g_restartAt = millis() + 900;
}



// ------------------------
void ESPWebDAV::handleSetSerial()	{
// ------------------------
	// change the UART baud used for gcode/data - applies live and persists
	uint32_t b = (uint32_t) queryParam("baud").toInt();
	static const uint32_t OK_BAUDS[] = { 9600, 19200, 38400, 57600, 74880, 115200, 230400, 250000, 460800, 921600 };
	bool valid = false;
	for(unsigned int i = 0; i < sizeof(OK_BAUDS)/sizeof(OK_BAUDS[0]); i++)
		if(b == OK_BAUDS[i]) { valid = true; break; }

	if(!valid)	{
		keepClient = false;
		return send("400 Bad Request", "application/json", "{\"ok\":0,\"err\":\"bad baud\"}");
	}

	config.setBaud(b);
	Serial.flush();
	Serial.end();
	Serial.begin(b);

	snprintf_P(s_scratch, sizeof(s_scratch), PSTR("{\"ok\":1,\"baud\":%lu}"), (unsigned long) b);
	sendHeader("Cache-Control", "no-cache");
	send("200 OK", "application/json", String(s_scratch));
}



// ------------------------
void ESPWebDAV::handleOtaUpdate()	{
// ------------------------
	// Raw-body firmware upload from the web UI. Validation layers:
	// browser pre-checks size + 0xE9 magic; Updater re-checks the magic on
	// the first block and the free sketch space; we require exact-length
	// delivery and a sane size before touching flash at all.
	size_t len = bodyRemaining;
	if(chunkedBody || len < 100000 || len > 921600)	{
		keepClient = false;
		return send("400 Bad Request", "application/json", "{\"ok\":0,\"err\":\"bad size\"}");
	}

	DBG_PRINTLN("Web OTA update starting");
	sendContinueIfNeeded();

	if(!Update.begin(len, U_FLASH))	{
		keepClient = false;
		snprintf_P(s_scratch, sizeof(s_scratch), PSTR("{\"ok\":0,\"err\":\"begin\",\"code\":%d}"), (int) Update.getError());
		return send("500 Internal Server Error", "application/json", String(s_scratch));
	}

	size_t remaining = len;
	while(remaining > 0)	{
		size_t want = remaining > sizeof(s_buf) ? sizeof(s_buf) : remaining;
		size_t got = readBytesWithTimeout(s_buf, want);
		bodyRemaining -= got;

		if(got != want)	{
			Update.end(false);
			keepClient = false;
			return send("500 Internal Server Error", "application/json", "{\"ok\":0,\"err\":\"timeout\"}");
		}
		if(Update.write(s_buf, got) != got)	{
			Update.end(false);
			keepClient = false;
			snprintf_P(s_scratch, sizeof(s_scratch), PSTR("{\"ok\":0,\"err\":\"write\",\"code\":%d}"), (int) Update.getError());
			return send("500 Internal Server Error", "application/json", String(s_scratch));
		}
		remaining -= got;
		yield();
	}

	if(!Update.end())	{
		keepClient = false;
		snprintf_P(s_scratch, sizeof(s_scratch), PSTR("{\"ok\":0,\"err\":\"end\",\"code\":%d}"), (int) Update.getError());
		return send("500 Internal Server Error", "application/json", String(s_scratch));
	}

	DBG_PRINTLN("Web OTA update complete");
	sendHeader("Cache-Control", "no-cache");
	send("200 OK", "application/json", "{\"ok\":1,\"restart\":1}");
	g_restartAt = millis() + 1200;	// flush the response, then reboot into new FW
}



// ------------------------
void ESPWebDAV::handleGet(ResourceType resource, bool isGet)	{
// ------------------------
	DBG_PRINTLN("Processing GET");

	// does URI refer to an existing file resource
	if(resource != RESOURCE_FILE)
		return handleNotFound();

	SdFile rFile;
	long tStart = millis();
	if(!rFile.open(uri.c_str(), O_READ))
		return handleNotFound();

	sendHeader("Allow", "PROPFIND,OPTIONS,DELETE,COPY,MOVE,HEAD,POST,PUT,GET");
	size_t fileSize = rFile.fileSize();
	setContentLength(fileSize);
	const char *contentType = getMimeType(uri.c_str());
	if(uri.endsWith(".gz") && strcmp(contentType, "application/x-gzip") != 0 && strcmp(contentType, "application/octet-stream") != 0)
		sendHeader("Content-Encoding", "gzip");

	send("200 OK", contentType, "");

	if(isGet)	{
		// Double-buffered streaming ("soft-DMA"): client.write() returns as
		// soon as the data is queued in the TCP send buffer, so the SD read
		// of the next chunk below overlaps with the radio transmitting the
		// queued one. (Slicing writes by availableForWrite() was tried first
		// but generates tiny TCP segments with TCP_NODELAY and kills speed.)
		const size_t HALF = sizeof(s_buf) / 2;	// 2048 = 4 full sectors
		uint8_t *cur = s_buf, *nxt = s_buf + HALF;
		int curLen = rFile.read(cur, HALF);

		// let Nagle coalesce partial segments during bulk streaming - with
		// NODELAY every 2048B write ships a runt 588B segment that wrecks
		// the ACK cadence against the tiny 2-segment lwIP send buffer
		client.setNoDelay(false);

		while(curLen > 0)	{
			if(!client.connected())	{
				// client went away - stop wasting SD/CPU time on a dead socket
				keepClient = false;
				break;
			}
			size_t written = client.write(cur, (size_t) curLen);
			if(written != (size_t) curLen)	{
				keepClient = false;
				break;
			}

			// prefetch the next chunk while TCP drains the queued one
			int nxtLen = rFile.available() ? rFile.read(nxt, HALF) : 0;
			uint8_t *t = cur; cur = nxt; nxt = t;
			curLen = nxtLen;
			yield();
		}

		client.setNoDelay(true);	// back to low-latency mode for small requests
	}

	rFile.close();
	(void) tStart;
	DBG_PRINT("File "); DBG_PRINT(fileSize); DBG_PRINT(" bytes sent in: "); DBG_PRINT((millis() - tStart)/1000); DBG_PRINTLN(" sec");
}




// ------------------------
void ESPWebDAV::handlePut(ResourceType resource)	{
// ------------------------
	DBG_PRINTLN("Processing Put");

	// does URI refer to a directory
	if(resource == RESOURCE_DIR)
		return handleNotFound();

	sendHeader("Allow", "PROPFIND,OPTIONS,DELETE,COPY,MOVE,HEAD,POST,PUT,GET");

	// chunked upload (e.g. macOS Finder) has no Content-Length up front
	if(chunkedBody)
		return handlePutChunked(resource);

	size_t contentLen = bodyRemaining;
	SdFile nFile;

	if(contentLen == 0)	{
		// create / truncate an empty file
		if(!nFile.open(uri.c_str(), O_CREAT | O_TRUNC | O_WRITE))
			return handleWriteError("Unable to create a new file", &nFile);
		nFile.close();
	}
	else	{
		if(contentLen > 0xFFFFF000UL)	{
			keepClient = false;
			return send("413 Payload Too Large", "text/plain", "File too large");
		}

		DBG_PRINT(uri); DBG_PRINTLN(" - ready for data");
		sendContinueIfNeeded();

		long tStart = millis();
		size_t numRemaining = contentLen;

		// high speed raw write implementation: delete old file and
		// pre-allocate a contiguous region we can stream into
		sd.remove(uri.c_str());

		uint32_t contBlocks = (contentLen + 511) / 512;
		uint32_t bgnBlock, endBlock;

		if (!nFile.createContiguous(sd.vwd(), uri.c_str(), contBlocks * 512UL))
			return handleWriteError("File create contiguous sections failed", &nFile);

		// get the location of the file's blocks
		if (!nFile.contiguousRange(&bgnBlock, &endBlock))
			return handleWriteError("Unable to get contiguous range", &nFile);

		if (!sd.card()->writeStart(bgnBlock, contBlocks))
			return handleWriteError("Unable to start writing contiguous range", &nFile);

		// read data from stream and write to the file.
		// readBytesWithTimeout() now *fills* the buffer (loops until complete
		// or stalled) - the old version could return a partial read and then
		// silently misalign the rest of the file = corrupted uploads.
		while(numRemaining > 0)	{
			size_t want = numRemaining > sizeof(s_buf) ? sizeof(s_buf) : numRemaining;
			size_t got = readBytesWithTimeout(s_buf, want);
			bodyRemaining -= got;

			if(got != want)	{
				sd.card()->writeStop();
				return handleWriteError("Timed out waiting for data", &nFile);
			}

			// zero-pad the final partial sector, then write full sectors
			size_t padded = (got + 511) & ~(size_t)511;
			if(padded > got)
				memset(s_buf + got, 0, padded - got);

			for(size_t off = 0; off < padded; off += 512)	{
				if (!sd.card()->writeData(s_buf + off))	{
					sd.card()->writeStop();
					return handleWriteError("Write data failed", &nFile);
				}
			}

			numRemaining -= got;
			yield();
		}

		// stop writing operation
		if (!sd.card()->writeStop())
			return handleWriteError("Unable to stop writing contiguous range", &nFile);

		// truncate the file to right length
		if(!nFile.truncate(contentLen))
			return handleWriteError("Unable to truncate the file", &nFile);

		nFile.close();

		(void) tStart;
		DBG_PRINT("File "); DBG_PRINT(contentLen); DBG_PRINT(" bytes stored in: "); DBG_PRINT((millis() - tStart)/1000); DBG_PRINTLN(" sec");
	}

	if(resource == RESOURCE_NONE)
		send("201 Created", NULL, "");
	else
		send("200 OK", NULL, "");
}



// ------------------------
void ESPWebDAV::handlePutChunked(ResourceType resource)	{
// ------------------------
	DBG_PRINTLN("Processing chunked Put");

	SdFile nFile;
	if(!nFile.open(uri.c_str(), O_CREAT | O_TRUNC | O_WRITE))
		return handleWriteError("Unable to create a new file", &nFile);

	sendContinueIfNeeded();

	while(true)	{
		long chunk = readChunkSize();
		if(chunk < 0)
			return handleWriteError("Timed out waiting for chunk", &nFile);
		if(chunk == 0)	{
			// consume optional trailer lines up to the blank one
			for(int i = 0; i < 4; i++)	{
				String l = client.readStringUntil('\n');
				if(l.length() <= 1)
					break;
			}
			chunkedBody = false;	// body fully consumed - connection stays usable
			break;
		}

		while(chunk > 0)	{
			size_t want = (size_t) chunk > sizeof(s_buf) ? sizeof(s_buf) : (size_t) chunk;
			size_t got = readBytesWithTimeout(s_buf, want);
			if(got != want)
				return handleWriteError("Timed out waiting for data", &nFile);
			if(nFile.write(s_buf, got) != got)
				return handleWriteError("Write data failed", &nFile);
			chunk -= got;
		}
		yield();
	}

	nFile.close();

	if(resource == RESOURCE_NONE)
		send("201 Created", NULL, "");
	else
		send("200 OK", NULL, "");
}




// ------------------------
void ESPWebDAV::handleWriteError(const char *message, FatFile *wFile)	{
// ------------------------
	// close this file
	wFile->close();
	// delete the file being written
	sd.remove(uri.c_str());
	// something is off - don't trust this connection anymore
	keepClient = false;
	// send error
	send("500 Internal Server Error", "text/plain", message);
	DBG_PRINTLN(message);
}


// ------------------------
void ESPWebDAV::handleDirectoryCreate(ResourceType resource)	{
// ------------------------
	DBG_PRINTLN("Processing MKCOL");

	// does URI refer to anything
	if(resource != RESOURCE_NONE)
		return handleNotFound();

	// create directory
	if (!sd.mkdir(uri.c_str(), true)) {
		// send error
		send("500 Internal Server Error", "text/plain", "Unable to create directory");
		DBG_PRINTLN("Unable to create directory");
		return;
	}

	DBG_PRINT(uri);	DBG_PRINTLN(" directory created");
	sendHeader("Allow", "OPTIONS,MKCOL,LOCK,POST,PUT");
	send("201 Created", NULL, "");
}



// ------------------------
void ESPWebDAV::handleMove(ResourceType resource)	{
// ------------------------
	DBG_PRINTLN("Processing MOVE");

	// does URI refer to anything
	if(resource == RESOURCE_NONE)
		return handleNotFound();

	if(destinationHeader.length() == 0)
		return handleNotFound();

	// old code forgot to url-decode -> "my file" became "my%20file" on rename
	String dest = urlDecode(urlToUri(destinationHeader));

	DBG_PRINT("Move destination: "); DBG_PRINTLN(dest);

	// per RFC the default is Overwrite: T
	FatFile dFile;
	if(dFile.open(sd.vwd(), dest.c_str(), O_READ))	{
		bool isDir = dFile.isDir();
		dFile.close();
		if(overwriteHeader.equalsIgnoreCase("F"))
			return send("412 Precondition Failed", "text/plain", "Destination exists");
		if(isDir)
			sd.rmdir(dest.c_str());
		else
			sd.remove(dest.c_str());
	}

	// move file or directory
	if ( !sd.rename(uri.c_str(), dest.c_str())	) {
		// send error
		send("500 Internal Server Error", "text/plain", "Unable to move");
		DBG_PRINTLN("Unable to move file/directory");
		return;
	}

	DBG_PRINTLN("Move successful");
	sendHeader("Allow", "OPTIONS,MKCOL,LOCK,POST,PUT");
	send("201 Created", NULL, "");
}




// ------------------------
// depth-first recursive delete, watchdog-fed, depth-capped for stack safety.
// Uses open-by-index + remove like SdFat's own rmRfStar, but yields per entry.
static bool rmTree(FatFile *dir, int depth)	{
	if(depth > 6)
		return false;
	SdFile entry;
	while(entry.openNext(dir, O_READ))	{
		yield();
		if(entry.isDir())	{
			if(!rmTree(&entry, depth + 1) || !entry.rmdir())	{
				entry.close();
				return false;
			}
		}
		else	{
			uint16_t idx = entry.dirIndex();
			entry.close();
			SdFile victim;
			if(!victim.open(dir, idx, O_WRITE) || !victim.remove())	{
				victim.close();
				return false;
			}
		}
	}
	return true;
}

// ------------------------
void ESPWebDAV::handleDelete(ResourceType resource)	{
// ------------------------
	DBG_PRINTLN("Processing DELETE");

	// does URI refer to anything
	if(resource == RESOURCE_NONE)
		return handleNotFound();

	bool retVal;

	if(resource == RESOURCE_FILE)
		// delete a file
		retVal = sd.remove(uri.c_str());
	else	{
		// delete a directory WITH its contents (real file-manager behavior,
		// and what RFC 4918 says DELETE on a collection should do).
		// Deleting "/" still fails safely - rmdir() refuses the root.
		SdFile dirFile;
		retVal = false;
		if(dirFile.open(uri.c_str(), O_READ))	{
			if(rmTree(&dirFile, 0))
				retVal = dirFile.rmdir();
			else
				dirFile.close();
		}
	}

	if(!retVal)	{
		// send error
		send("500 Internal Server Error", "text/plain", "Unable to delete");
		DBG_PRINTLN("Unable to delete file/directory");
		return;
	}

	DBG_PRINTLN("Delete successful");
	sendHeader("Allow", "OPTIONS,MKCOL,LOCK,POST,PUT");
	send("200 OK", NULL, "");
}

ESPWebDAV dav;

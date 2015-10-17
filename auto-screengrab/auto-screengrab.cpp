// auto-screengrab.cpp : Defines the entry point for the application.
//
#define SECURITY_WIN32
#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers

#include <windows.h>
#include <dxgi.h>
#include <dxgi1_4.h>

#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>
#include <time.h>

#include "log.h"
#include "curl/curl.h"

#include <stdio.h>
#include <atlbase.h>
#include <d3d11.h>
#include <wincodec.h>

#include <chrono>
#include <random>

#define CHECK(x) do { HRESULT hr = x; if (FAILED(hr)) { aslog::error(#x L": 0x%.8x", hr); return hr;} } while (0)

CComPtr<IDXGIAdapter1> g_pAdapter;
CComPtr<IDXGIOutput1> g_pOutput;
CComPtr<ID3D11Device> g_pDevice;
CComPtr<ID3D11DeviceContext> g_pContext;
CComPtr<IWICImagingFactory> g_pImagingFactory;

char g_smtpURL[128];
char g_smtpUser[128];
char g_smtpPass[128];
char g_smtpTo[128];
char g_smtpFrom[128];

static HRESULT find_output()
{
	CComPtr<IDXGIFactory1> pFactory;
	HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **)(&pFactory));
	for (UINT i = 0; ; i++) {
		CComPtr<IDXGIAdapter1> pAdapter;
		if (S_OK != (hr = pFactory->EnumAdapters1(i, &pAdapter))) {
			break;
		}

		aslog::info(L"Found adapter %d", i);

		for (UINT j = 0; ; j++) {
			CComPtr<IDXGIOutput> pOutput;
			if (S_OK != (hr = pAdapter->EnumOutputs(j, &pOutput))) {
				break;
			}

			aslog::info(L"Found output %d-%d", i, j);
			DXGI_OUTPUT_DESC desc;
			pOutput->GetDesc(&desc);
			aslog::info(L"Output %d-%d name: %s", i, j, desc.DeviceName);
			aslog::info(L"Output %d-%d attached to desktop: %s", i, j, desc.AttachedToDesktop ? L"true" : L"false");

			g_pAdapter = pAdapter;
			g_pOutput = pOutput;
			hr = D3D11CreateDevice(pAdapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, D3D11_CREATE_DEVICE_DEBUG, NULL, 0, D3D11_SDK_VERSION, &g_pDevice, NULL, &g_pContext);
			return hr;
		}
	}
	return hr;
}

static HRESULT get_screen_data(CComPtr<IDXGIResource> &pResource, UINT8 **data, UINT *width, UINT *height)
{
	CComPtr<ID3D11Texture2D> pTexture;
	CHECK(pResource->QueryInterface(&pTexture));

	D3D11_TEXTURE2D_DESC desc;
	pTexture->GetDesc(&desc);
	*width = desc.Width;
	*height = desc.Height;

	CComPtr<ID3D11Texture2D> pStaging;
	desc.BindFlags = 0;
	desc.MiscFlags = 0;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
	desc.Usage = D3D11_USAGE_STAGING;
	CHECK(g_pDevice->CreateTexture2D(&desc, NULL, &pStaging));

	g_pContext->CopyResource(pStaging, pTexture);
	D3D11_MAPPED_SUBRESOURCE mapped;
	CHECK(g_pContext->Map(pStaging, D3D11CalcSubresource(0, 0, 1), D3D11_MAP_READ_WRITE, 0, &mapped));

	*data = new UINT8[mapped.DepthPitch];
	memcpy(*data, mapped.pData, mapped.DepthPitch);
	g_pContext->Unmap(pStaging, 0);
	return S_OK;
}

HRESULT encode_jpeg(UINT8 *data, UINT width, UINT height, UINT8 **jpeg_data, DWORD *sz)
{
	CComPtr<IStream> pStream(SHCreateMemStream(NULL, 0));
	
	{
		CComPtr<IWICBitmapEncoder> pEncoder;
		CHECK(g_pImagingFactory->CreateEncoder(GUID_ContainerFormatJpeg, NULL, &pEncoder));
		CHECK(pEncoder->Initialize(pStream, WICBitmapEncoderCacheOption::WICBitmapEncoderNoCache));

		CComPtr<IWICBitmapFrameEncode> pFrame;
		CHECK(pEncoder->CreateNewFrame(&pFrame, NULL));
		CHECK(pFrame->Initialize(NULL));

		CComPtr<IWICBitmap> pBitmap;
		CHECK(g_pImagingFactory->CreateBitmapFromMemory(width, height, GUID_WICPixelFormat32bppBGRA, width * 4, width * height * 4, data, &pBitmap));
		CHECK(pFrame->WriteSource(pBitmap, NULL));
		CHECK(pFrame->Commit());
		CHECK(pEncoder->Commit());
	}

	STATSTG statstg;
	CHECK(pStream->Stat(&statstg, STATFLAG_NONAME));

	LARGE_INTEGER l;
	l.QuadPart = 0;
	CHECK(pStream->Seek(l, STREAM_SEEK_SET, NULL));

	*sz = statstg.cbSize.LowPart;
	*jpeg_data = new UINT8[*sz];
	ULONG dummy;
	CHECK(pStream->Read(*jpeg_data, statstg.cbSize.LowPart, &dummy));
	
	return S_OK;
}

char *byteLookup[] = {
	"=00", "=01", "=02", "=03", "=04", "=05", "=06", "=07", "=08", "=09", "=0A", "=0B", "=0C", "=0D", "=0E", "=0F",
	"=10", "=11", "=12", "=13", "=14", "=15", "=16", "=17", "=18", "=19", "=1A", "=1B", "=1C", "=1D", "=1E", "=1F",
	"=20", "=21", "=22", "=23", "=24", "=25", "=26", "=27", "=28", "=29", "=2A", "=2B", "=2C", "=2D", "=2E", "=2F",
	"=30", "=31", "=32", "=33", "=34", "=35", "=36", "=37", "=38", "=39", "=3A", "=3B", "=3C", "=3D", "=3E", "=3F",
	"=40", "=41", "=42", "=43", "=44", "=45", "=46", "=47", "=48", "=49", "=4A", "=4B", "=4C", "=4D", "=4E", "=4F",
	"=50", "=51", "=52", "=53", "=54", "=55", "=56", "=57", "=58", "=59", "=5A", "=5B", "=5C", "=5D", "=5E", "=5F",
	"=60", "=61", "=62", "=63", "=64", "=65", "=66", "=67", "=68", "=69", "=6A", "=6B", "=6C", "=6D", "=6E", "=6F",
	"=70", "=71", "=72", "=73", "=74", "=75", "=76", "=77", "=78", "=79", "=7A", "=7B", "=7C", "=7D", "=7E", "=7F",
	"=80", "=81", "=82", "=83", "=84", "=85", "=86", "=87", "=88", "=89", "=8A", "=8B", "=8C", "=8D", "=8E", "=8F",
	"=90", "=91", "=92", "=93", "=94", "=95", "=96", "=97", "=98", "=99", "=9A", "=9B", "=9C", "=9D", "=9E", "=9F",
	"=A0", "=A1", "=A2", "=A3", "=A4", "=A5", "=A6", "=A7", "=A8", "=A9", "=AA", "=AB", "=AC", "=AD", "=AE", "=AF",
	"=B0", "=B1", "=B2", "=B3", "=B4", "=B5", "=B6", "=B7", "=B8", "=B9", "=BA", "=BB", "=BC", "=BD", "=BE", "=BF",
	"=C0", "=C1", "=C2", "=C3", "=C4", "=C5", "=C6", "=C7", "=C8", "=C9", "=CA", "=CB", "=CC", "=CD", "=CE", "=CF",
	"=D0", "=D1", "=D2", "=D3", "=D4", "=D5", "=D6", "=D7", "=D8", "=D9", "=DA", "=DB", "=DC", "=DD", "=DE", "=DF",
	"=E0", "=E1", "=E2", "=E3", "=E4", "=E5", "=E6", "=E7", "=E8", "=E9", "=EA", "=EB", "=EC", "=ED", "=EE", "=EF",
	"=F0", "=F1", "=F2", "=F3", "=F4", "=F5", "=F6", "=F7", "=F8", "=F9", "=FA", "=FB", "=FC", "=FD", "=FE", "=FF",
};

HRESULT build_msg(CComPtr<IStream> &stream, UINT8 *jpeg_data, DWORD sz)
{
#define WRITE(str) do { char *tmp = str; stream->Write(tmp, strlen(tmp), NULL); } while (0)
	time_t t = time(NULL);
	tm *mytm = localtime(&t);
	char rfc822date[128] = { 0 };
	strftime(rfc822date, 128, "%a, %d %b %Y %H:%M:%S %z", mytm);

	WRITE("Date: "); WRITE(rfc822date); WRITE("\r\n");
	WRITE("From: "); WRITE(g_smtpFrom); WRITE("\r\n");
	WRITE("To: "); WRITE(g_smtpTo); WRITE("\r\n");
	WRITE("Subject: Screenshot for "); WRITE(rfc822date); WRITE("\r\n");

	GUID guid = { 0 };
	wchar_t guidW[40] = { 0 };
	char guidA[40] = { 0 };
	CoCreateGuid(&guid);
	StringFromGUID2(guid, guidW, 40);
	WideCharToMultiByte(0, 0, guidW, -1, guidA, 40, NULL, NULL);
	WRITE("Message-Id: <"); WRITE(guidA); WRITE(">\r\n");

	WRITE("MIME-Version: 1.0\r\n");
	WRITE("Content-Type: multipart/mixed; boundary="); WRITE(guidA); WRITE("\r\n");
	WRITE("\r\n");

	WRITE("--"); WRITE(guidA); WRITE("\r\n");
	WRITE("Content-Type: text/plain\r\n");
	WRITE("\r\n");
	WRITE("Screenshot is attached\r\n");

	WRITE("--"); WRITE(guidA); WRITE("\r\n");
	WRITE("Content-Type: image/jpeg\r\n");
	WRITE("Content-Transfer-Encoding: quoted-printable\r\n");
	WRITE("Content-Disposition: inline\r\n");
	WRITE("\r\n");

	UINT8 *p = jpeg_data;
	for (int i = 0; i < sz; i++) {
		WRITE(byteLookup[*p++]);
		if ((i + 1) % 24 == 0) {
			WRITE("=\r\n");
		}
	}

	WRITE("\r\n--"); WRITE(guidA); WRITE("--\r\n");

	LARGE_INTEGER pos = { 0 };
	stream->Seek(pos, STREAM_SEEK_SET, NULL);
	return S_OK;
#undef WRITE
}

size_t payload_source(char *buffer, size_t size, size_t nitems, void *instream)
{
	IStream *stream = (IStream*) instream;
	ULONG cbRead;
	stream->Read(buffer, size * nitems, &cbRead);
	return cbRead;
}

HRESULT send_email(UINT8 *jpeg_data, DWORD sz)
{
	CURL *curl = curl_easy_init();
	if (!curl) {
		aslog::error(L"curl_easy_init failed");
		return MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0xFF00);
	}
	curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

	CComPtr<IStream> stream = SHCreateMemStream(NULL, 0);
	build_msg(stream, jpeg_data, sz);

	curl_easy_setopt(curl, CURLOPT_READFUNCTION, payload_source);
	curl_easy_setopt(curl, CURLOPT_READDATA, stream.p);
	curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);

	curl_easy_setopt(curl, CURLOPT_URL, g_smtpURL);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

	curl_easy_setopt(curl, CURLOPT_USERNAME, g_smtpUser);
	curl_easy_setopt(curl, CURLOPT_PASSWORD, g_smtpPass);
	curl_easy_setopt(curl, CURLOPT_MAIL_FROM, g_smtpFrom);

	struct curl_slist *recipients = NULL;
	recipients = curl_slist_append(recipients, g_smtpTo);
	curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);

	char errbufA[CURL_ERROR_SIZE];
	errbufA[0] = 0;
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbufA);

	CURLcode res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		wchar_t errbufW[CURL_ERROR_SIZE];
		MultiByteToWideChar(CP_UTF8, 0, errbufA, CURL_ERROR_SIZE, errbufW, CURL_ERROR_SIZE);
		aslog::error(L"Mail sending failed: %s", errbufW);
	}
	curl_slist_free_all(recipients);
	curl_easy_cleanup(curl);
	return S_OK;
}

void load_settings()
{
	HKEY hkey;
	RegOpenKeyEx(HKEY_CURRENT_USER, L"SOFTWARE\\auto-screengrab", 0, KEY_QUERY_VALUE, &hkey);

	DWORD sz = 128;
	RegGetValueA(hkey, NULL, "smtp_url", RRF_RT_REG_SZ, NULL, &g_smtpURL, &sz);
	sz = 128;
	RegGetValueA(hkey, NULL, "smtp_user", RRF_RT_REG_SZ, NULL, &g_smtpUser, &sz);
	sz = 128;
	RegGetValueA(hkey, NULL, "smtp_pass", RRF_RT_REG_SZ, NULL, &g_smtpPass, &sz);
	sz = 128;
	RegGetValueA(hkey, NULL, "smtp_from", RRF_RT_REG_SZ, NULL, &g_smtpFrom, &sz);
	sz = 128;
	RegGetValueA(hkey, NULL, "smtp_to", RRF_RT_REG_SZ, NULL, &g_smtpTo, &sz);
	sz = 128;
	RegGetValueA(hkey, NULL, "smtp_url", RRF_RT_REG_SZ, NULL, &g_smtpURL, &sz);

	RegCloseKey(hkey);
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
	CoInitializeEx(NULL, COINIT_MULTITHREADED);
	aslog::openlog();
	aslog::setlevel(aslog::level::debug);

	curl_global_init(CURL_GLOBAL_ALL);
	load_settings();
	HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory,
								  NULL,
								  CLSCTX_INPROC_SERVER,
								  IID_PPV_ARGS(&g_pImagingFactory)
								  );

	hr = find_output();
	CComPtr<IDXGIOutputDuplication> pDup;
	std::mt19937 rand(std::chrono::high_resolution_clock::now().time_since_epoch().count());

	while (true) {
		Sleep((rand() % 30 + 1) * 60000 + (rand() % 30 + 1) * 1000);

		DXGI_OUTDUPL_FRAME_INFO frameInfo;
		CComPtr<IDXGIResource> pResource;

		if (!pDup) {
			hr = g_pOutput->DuplicateOutput(g_pDevice, &pDup);
			// First image captured is always black
			hr = pDup->AcquireNextFrame(1000, &frameInfo, &pResource);
			pDup->ReleaseFrame();
			pResource.Release();
		}

		hr = pDup->AcquireNextFrame(1000, &frameInfo, &pResource);
		if (hr == DXGI_ERROR_ACCESS_LOST) {
			pDup.Release();
			continue;
		} else if (FAILED(hr)) {
			aslog::error(L"Unable to capture frame: 0x%.8x", hr);
			Sleep(1000);
			continue;
		} else {
			aslog::info(L"Captured frame successfully");

			DWORD sz;
			UINT8 *data, *jpeg_data;
			UINT width, height;
			get_screen_data(pResource, &data, &width, &height);
			pResource.Release();
			pDup->ReleaseFrame();

			encode_jpeg(data, width, height, &jpeg_data, &sz);
			delete[] data;

			send_email(jpeg_data, sz);
			delete[] jpeg_data;
		}
	}

	aslog::closelog();
	return 0;
}

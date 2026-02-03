#define UNICODE
#define _UNICODE

/**************************************************************************************************
 * System libs
 **************************************************************************************************/
#include <windows.h>
#include <Shlobj.h>
#include <wincodec.h>
#include <Tlhelp32.h>
#include <CommCtrl.h>
#include <strsafe.h>
#pragma comment(lib, "Comctl32.lib")

 /**************************************************************************************************
  * Standard libs
  **************************************************************************************************/
#include <cstdio>
#include <vector>
#include <string>
#include <unordered_map>

#include "resource.h" // for version info

#include <wingdi.h>
#pragma comment(lib, "Msimg32.lib")


  /**************************************************************************************************
   * Simple types and constants
   **************************************************************************************************/
typedef wchar_t                 Char;
typedef unsigned char           Byte;
typedef __time64_t              Time;
typedef std::wstring            String;
typedef std::vector<String>     StringList;

const String CACHE_FILE_NAME = L"!stacky.cache";
const String STACKY_EXEC_NAME = L"stacky.exe";
const Char* STACKY_WINDOW_NAME = L"stacky";
const Char* DIR_SEP = L"\\";
const String SUBMENU_SUFFIX = L".submenu";
const String DESKTOP_INI = L"desktop.ini";
const DWORD CACHE_VERSION = 9; // Increment this when cache format changes

enum {
	WM_BASE = WM_USER + 100,
	WM_OPEN_TARGET_FOLDER = WM_BASE + 1,
	WM_MENU_ITEM = WM_BASE + 2,
	WM_OPEN_LOCATION = WM_BASE + 3,

	APP_EXIT_DELAY = 3 * 1000,

	ERR_PATH_MISSING = 401,
	ERR_PATH_INVALID = 402,
	ERR_PARAM_UNKNOWN = 403,
};


/**************************************************************************************************
 * COM init (once)
 **************************************************************************************************/
struct ComInit {
	HRESULT hr;
	ComInit() : hr(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
	~ComInit() { if (SUCCEEDED(hr)) CoUninitialize(); }
};


/**************************************************************************************************
 * The meat
 **************************************************************************************************/

struct Util {

	static String rtrim(const String& target, const String& trim) {
		size_t cutoff_pos = target.size() - trim.size();
		return target.rfind(trim) == cutoff_pos ? target.substr(0, cutoff_pos) : target;
	}
	static String ltrim(const String& target, const String& trim) {
		size_t cutoff_pos = trim.size();
		return target.find(trim) != String::npos ? target.substr(cutoff_pos) : target;
	}
	static String trim(const String& target, const String& trim) {
		return rtrim(ltrim(target, trim), trim);
	}
	static String quote(const String& target) {
		return L"\"" + target + L"\"";
	}
	static bool ends_with(const String& target, const String& ending)
	{
		if (target.length() >= ending.length())
		{
			return (0 == target.compare(target.length() - ending.length(), ending.length(), ending));
		}
		else
		{
			return false;
		}
	}

	static void kill_other_stackies() {
		PROCESSENTRY32 entry = { 0 };
		entry.dwSize = sizeof(PROCESSENTRY32);
		BOOL found = false;
		HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPALL, 0);
		do {
			found = ::Process32Next(snapshot, &entry);
			if (entry.th32ProcessID != ::GetCurrentProcessId() && entry.szExeFile == STACKY_EXEC_NAME) {
				HANDLE hOtherStacky = ::OpenProcess(PROCESS_ALL_ACCESS, FALSE, entry.th32ProcessID);
				if (hOtherStacky) {
					::TerminateProcess(hOtherStacky, 0);
					::CloseHandle(hOtherStacky);
				}
				else {
					Util::msg(L"Failed to open another stacky.exe process. Kill stacky.exe manually.");
				}
			}
		} while (found);
		::CloseHandle(snapshot);
	}
	static Time get_modified(const String& file_path) {
		struct _stat buf;
		return _wstat(file_path.c_str(), &buf) ? 0 : buf.st_mtime;
	}
	static int parse_cmd_line(const String& cmd_line, String& stack_path, String& opts) {
		stack_path = cmd_line;
		opts = L"";

		if (stack_path.size() < 1) {
			return ERR_PATH_MISSING;
		}

		// Check for options (arguments starting with --)
		size_t option_pos = stack_path.find(L" --");
		if (option_pos != String::npos) {
			opts = stack_path.substr(option_pos + 1); // Keep the space for trimming
			stack_path = stack_path.substr(0, option_pos);
		}

		DWORD attrs = ::GetFileAttributes(trim(stack_path, L"\"").c_str());
		if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
			return ERR_PATH_INVALID;
		}
		return 0;
	}
	static void msgt(const String& title, const wchar_t* format, ...) {
		static Char msgBuf[4096] = { 0 };

		va_list arglist;
		va_start(arglist, format);
		vswprintf(msgBuf, format, arglist);
		va_end(arglist);

		::MessageBox(0, msgBuf, title.c_str(), MB_OK | MB_ICONINFORMATION);
	}

	static void msg(const wchar_t* format, ...) {
		static Char msgBuf[4096] = { 0 };

		va_list arglist;
		va_start(arglist, format);
		vswprintf(msgBuf, format, arglist);
		va_end(arglist);

		::MessageBox(0, msgBuf, L"Stacky", MB_OK | MB_ICONINFORMATION);
	}

	static HRESULT ResolveShortcut(HWND hwnd, LPCTSTR lpszLinkFile, LPTSTR lpszPath, int iPathBufferSize)
	{
		if (lpszPath == NULL)
			return E_INVALIDARG;

		*lpszPath = 0;

		// Get a pointer to the IShellLink interface. It is assumed that CoInitialize
		// has already been called.
		IShellLink* psl = NULL;
		HRESULT hres = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (LPVOID*)&psl);
		if (SUCCEEDED(hres))
		{
			// Get a pointer to the IPersistFile interface.
			IPersistFile* ppf = NULL;
			hres = psl->QueryInterface(IID_IPersistFile, (void**)&ppf);
			if (SUCCEEDED(hres))
			{
				// Add code here to check return value from MultiByteWideChar
				// for success.

				// Load the shortcut.
#ifdef _UNICODE
				hres = ppf->Load(lpszLinkFile, STGM_READ);
#else
				WCHAR wsz[MAX_PATH] = { 0 };
				// Ensure that the string is Unicode.
				MultiByteToWideChar(CP_ACP, 0, lpszLinkFile, -1, wsz, MAX_PATH);
				hres = ppf->Load(wsz, STGM_READ);
#endif

				if (SUCCEEDED(hres))
				{
					// Resolve the link.
					hres = psl->Resolve(hwnd, 0);

					if (SUCCEEDED(hres))
					{
						// Get the path to the link target.
						TCHAR szGotPath[MAX_PATH] = { 0 };
						hres = psl->GetPath(szGotPath, _countof(szGotPath), NULL, SLGP_SHORTPATH);

						if (SUCCEEDED(hres))
						{
							hres = StringCbCopy(lpszPath, iPathBufferSize, szGotPath);
						}
					}
				}

				// Release the pointer to the IPersistFile interface.
				ppf->Release();
			}

			// Release the pointer to the IShellLink interface.
			psl->Release();
		}
		return hres;
	}

	// Read icon path from desktop.ini file
	static String ReadIconFromDesktopIni(const String& folder_path) {
		String desktop_ini_path = folder_path + DIR_SEP + DESKTOP_INI;
		DWORD attrs = ::GetFileAttributes(desktop_ini_path.c_str());
		if (attrs == INVALID_FILE_ATTRIBUTES) {
			return L"";
		}

		Char icon_file[MAX_PATH] = { 0 };
		Char icon_resource[MAX_PATH] = { 0 };

		// Read IconFile from desktop.ini
		::GetPrivateProfileString(L".ShellClassInfo", L"IconFile", L"", icon_file, MAX_PATH, desktop_ini_path.c_str());
		::GetPrivateProfileString(L".ShellClassInfo", L"IconResource", L"", icon_resource, MAX_PATH, desktop_ini_path.c_str());

		// Prefer IconResource over IconFile
		String icon_path = icon_resource[0] != 0 ? icon_resource : icon_file;

		if (icon_path.empty()) {
			return L"";
		}

		// If path is relative, make it absolute relative to the folder
		if (icon_path.find(L":") == String::npos && icon_path[0] != L'\\') {
			icon_path = folder_path + DIR_SEP + icon_path;
		}

		return icon_path;
	}

	// Get monitor from cursor position
	static HMONITOR GetMonitorFromCursor() {
		POINT pt;
		::GetCursorPos(&pt);
		return ::MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
	}

	// Get work area for a specific monitor
	static RECT GetWorkAreaForMonitor(HMONITOR hMonitor) {
		MONITORINFO mi = { sizeof(MONITORINFO) };
		::GetMonitorInfo(hMonitor, &mi);
		return mi.rcWork;
	}
};

struct Buffer {
	size_t  capacity, size;
	Byte* data;

	Buffer() : capacity(0), size(0), data(0) {}
	~Buffer() {}

	void free() {
		if (data) {
			delete[] data;
			data = 0;
		}
		capacity = size = 0;
	}
	bool load(const String& str, bool append_null) {
		load(str.c_str(), str.size() * sizeof(Char));
		if (append_null) {
			Char str_end[1] = { 0 };
			load(str_end, sizeof(Char));
		}
		return true;
	}
	bool load(const void* src, size_t src_size) {
		grow(src_size + size);
		memcpy(data + size, src, src_size);
		size += src_size;
		return true;
	}
	bool load(const String& file_path) {
		FileWrap f(file_path, L"rb");
		if (!f.is_open()) {
			return false;
		}
		size_t file_size = f.size();
		if (!file_size) {
			return false;
		}
		grow(file_size + size);
		f.read(data + size, file_size);
		size += file_size;
		return true;
	}
	bool save(const String& file_path) {
		FileWrap f(file_path, L"wb");
		if (!f.is_open()) {
			return false;
		}
		f.write(data, size);
		return true;
	}

private:
	struct FileWrap {
		FILE* f;
		FileWrap(const String& path, const String& mode) { f = _wfopen(path.c_str(), mode.c_str()); }
		~FileWrap() { f&& fclose(f); f = 0; }
		bool    is_open() { return f != 0; }
		size_t  write(Byte* data, size_t size) { return fwrite(data, 1, size, f); }
		size_t  read(Byte* data, size_t size) { return fread(data, 1, size, f); }
		size_t  size() {
			fseek(f, 0L, SEEK_END);
			size_t file_size = ftell(f);
			fseek(f, 0L, SEEK_SET);
			return file_size;
		}
	};
	void grow(size_t new_capacity) {
		if (capacity >= new_capacity) {
			return;
		}
		size_t new_cap = capacity ? capacity : 256;
		while (new_cap < new_capacity) {
			new_cap *= 2;
		}
		Byte* new_data = new Byte[new_cap];
		if (data) {
			memcpy(new_data, data, size);
			delete[] data;
		}
		data = new_data;
		capacity = new_cap;
	}
};

/**************************************************************************************************
 * Cache
 **************************************************************************************************/

struct Bmp {

	BITMAPFILEHEADER    file_header;
	BITMAPINFOHEADER    info_header;
	Buffer              bits;
	HBITMAP             hBmp;

	Bmp() : hBmp(0) {
		memset(&file_header, 0, sizeof(BITMAPFILEHEADER));
		memset(&info_header, 0, sizeof(BITMAPINFOHEADER));
	}

	void close() {
		::DeleteObject(hBmp);
		bits.free();
		hBmp = 0;
		memset(&file_header, 0, sizeof(BITMAPFILEHEADER));
		memset(&info_header, 0, sizeof(BITMAPINFOHEADER));
	}
	int total_size() {
		return file_header.bfSize;
	}
	int bits_size() {
		return file_header.bfSize - sizeof(BITMAPINFOHEADER) - sizeof(BITMAPFILEHEADER);
	}
	bool load_bits_and_headers(Byte* bytes) {
		close();
		int pos = 0;
		memcpy(&file_header, bytes + pos, sizeof(BITMAPFILEHEADER));
		pos += sizeof(BITMAPFILEHEADER);
		memcpy(&info_header, bytes + pos, sizeof(BITMAPINFOHEADER));
		pos += sizeof(BITMAPINFOHEADER);
		return fill_bitmap(bytes + pos, bits_size());
	}
	bool load_bits_only(Byte* bytes, int bits_size, int width, int height) {
		close();
		memset(&file_header, 0, sizeof(BITMAPFILEHEADER));
		file_header.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + bits_size;
		file_header.bfType = 0x4d42;
		file_header.bfOffBits = 0x36;
		info_header = create_info_header(width, height);
		return fill_bitmap(bytes, bits_size);
	}
	bool serialize(Buffer& buffer) {
		buffer.load(&file_header, sizeof(BITMAPFILEHEADER));
		buffer.load(&info_header, sizeof(BITMAPINFOHEADER));
		buffer.load(bits.data, bits.size);
		return true;
	}
	static BITMAPINFOHEADER create_info_header(int width, int height) {
		BITMAPINFOHEADER bmih = { 0 };
		bmih.biSize = sizeof(BITMAPINFOHEADER);
		bmih.biWidth = width;
		bmih.biHeight = height;
		bmih.biPlanes = 1;
		bmih.biBitCount = 32;
		bmih.biCompression = BI_RGB;
		return bmih;
	}
	static bool convert_file_icon(const HICON icon, Bmp& bmp) {
		static IWICImagingFactory* img_factory = 0;
		if (!img_factory) {
			// In VS 2011 beta, clsid has to be changed to CLSID_WICImagingFactory1 (from CLSID_WICImagingFactory)
			if (!SUCCEEDED(::CoInitialize(0)) || !SUCCEEDED(::CoCreateInstance(CLSID_WICImagingFactory1, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&img_factory)))) {
				return false;
			}
		}
		IWICBitmap* pBitmap = 0;
		IWICFormatConverter* pConverter = 0;
		UINT cx = 0, cy = 0;
		if (SUCCEEDED(img_factory->CreateBitmapFromHICON(icon, &pBitmap))) {
			if (SUCCEEDED(img_factory->CreateFormatConverter(&pConverter))) {
				if (SUCCEEDED(pConverter->Initialize(pBitmap, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, 0, 0.0f, WICBitmapPaletteTypeCustom))) {
					if (SUCCEEDED(pConverter->GetSize(&cx, &cy))) {
						const UINT stride = cx * sizeof(DWORD);
						const UINT buf_size = cy * stride;
						Byte* buf = new Byte[buf_size];
						pConverter->CopyPixels(0, stride, buf_size, buf);
						bmp.load_bits_only(buf, buf_size, cx, -(int)cy);
						delete[] buf;
					}
				}
				pConverter->Release();
			}
			pBitmap->Release();
		}
		::DestroyIcon(icon);

		return true;
	}
	static HICON extract_file_icon(const String& file_path) {
		SHFILEINFOW file_info = { 0 };
		HIMAGELIST hfi = (HIMAGELIST)::SHGetFileInfo(file_path.c_str(), 0, &file_info, sizeof(SHFILEINFOW), SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
		return ::ImageList_GetIcon(hfi, file_info.iIcon, ILD_NORMAL);
	}

	static HICON extract_icon_from_path_with_index(const String& icon_path) {
		HICON hIcon = 0;
		String path = icon_path;
		int icon_index = 0;

		// Check if path contains icon index (e.g., "path.dll,2")
		size_t comma_pos = icon_path.find(L',');
		if (comma_pos != String::npos) {
			path = icon_path.substr(0, comma_pos);
			icon_index = _wtoi(icon_path.substr(comma_pos + 1).c_str());
		}

		// Expand environment variables if present
		TCHAR expanded_path[MAX_PATH] = { 0 };
		::ExpandEnvironmentStrings(path.c_str(), expanded_path, MAX_PATH);
		path = expanded_path;

		// Extract small icon (16x16) instead of large icon (32x32)
		::ExtractIconEx(path.c_str(), icon_index, 0, &hIcon, 1);

		if (!hIcon) {
			// Fallback to default file icon
			hIcon = extract_file_icon(path);
		}

		return hIcon;
	}

private:
	bool fill_bitmap(void* bytes, int byte_count) {
		Byte* buf = 0;
		if (!create_bitmap(info_header.biWidth, info_header.biHeight, (void**)(&buf), &hBmp)) {
			return false;
		}
		memcpy(buf, bytes, byte_count);
		return bits.load(bytes, byte_count);
	}
	static bool create_bitmap(int width, int height, void** bits, HBITMAP* phBmp) {
		BITMAPINFO bmi = { 0 };
		bmi.bmiHeader = create_info_header(width, height);
		*phBmp = ::CreateDIBSection(GetDC(0), &bmi, DIB_RGB_COLORS, bits, 0, 0);
		return *phBmp != 0;
	}
};

struct Cache {

	struct Item {
		String  name;
		Bmp     bmp;
		bool    is_submenu;
		String  submenu_path;
		String  relative_path; // For items in submenus

		Item() : is_submenu(false) {}

		bool create(const String& file_name, const String& file_path) {
			name = file_name;
			is_submenu = false;
			submenu_path.clear();
			relative_path.clear();

			DWORD attrs = ::GetFileAttributes(file_path.c_str());
			if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {

				// Mark as submenu if needed
				if (Util::ends_with(file_name, SUBMENU_SUFFIX)) {
					is_submenu = true;
					submenu_path = file_path;
				}

				// For ANY folder: try custom icon from desktop.ini first
				String icon_path = Util::ReadIconFromDesktopIni(file_path);
				if (!icon_path.empty()) {
					HICON hIcon = Bmp::extract_icon_from_path_with_index(icon_path);
					if (hIcon) {
						if (Bmp::convert_file_icon(hIcon, bmp)) {
							return true;
						}
					}
				}

				// Fallback to normal folder icon
				if (Bmp::convert_file_icon(Bmp::extract_file_icon(file_path), bmp)) {
					return true;
				}
			}

			// Regular item - use original extraction method
			if (!Bmp::convert_file_icon(Bmp::extract_file_icon(file_path), bmp)) {
				return false;
			}

			return true;
		}
		void serialize(Buffer& buffer) {
			buffer.load(name, true);
			buffer.load(&is_submenu, sizeof(is_submenu));
			if (is_submenu) {
				buffer.load(submenu_path, true);
			}
			bmp.serialize(buffer);
		}
		void unserialize(Buffer& buffer, size_t& pos) {
			name = (Char*)(buffer.data + pos);
			pos += (name.size() + 1) * sizeof(Char);

			memcpy(&is_submenu, buffer.data + pos, sizeof(is_submenu));
			pos += sizeof(is_submenu);

			if (is_submenu) {
				submenu_path = (Char*)(buffer.data + pos);
				pos += (submenu_path.size() + 1) * sizeof(Char);
			}

			bmp.load_bits_and_headers(buffer.data + pos);
			pos += bmp.total_size();
		}
	};


	std::vector<Item>   items;
	int                 fixed_items;
	bool                was_rebuilt;
	String              base_dir;

	Cache(const String& stack_path) : last_modified(0), was_rebuilt(false), scanned_last_modified(0), fixed_items(0) {
		base_dir = Util::trim(Util::rtrim(stack_path, DIR_SEP), L"\"") + DIR_SEP;
		cache_path = path(CACHE_FILE_NAME);
	}

	String path(const String& file = L"") const {
		return base_dir + file;
	}

	bool scan() {
		return scan_directory(base_dir, L"");
	}

	bool scan_directory(const String& dir_path, const String& relative_path) {
		WIN32_FIND_DATA ffd = { 0 };
		HANDLE hfind = FindFirstFile((dir_path + L"*").c_str(), &ffd);
		if (hfind == INVALID_HANDLE_VALUE) {
			return false;
		}
		do {
			String filename = ffd.cFileName;
			if (filename == L"." || filename == L".." || ffd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN || Util::ends_with(filename, L".ignore") || filename == DESKTOP_INI)
				continue;

			String full_filename = relative_path + filename;
			scanned_items.push_back(full_filename);
			update_max_modified(full_filename);

			// If this is a .submenu folder, recursively scan it
			if ((ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
				Util::ends_with(filename, SUBMENU_SUFFIX)) {
				scan_directory(dir_path + filename + DIR_SEP, full_filename + DIR_SEP);
			}
		} while (FindNextFile(hfind, &ffd) != 0);
		FindClose(hfind);
		return true;
	}

	bool load() {
		Buffer buffer;
		items.clear();

		if (!buffer.load(cache_path)) {
			// Cache file doesn't exist, will rebuild
			rebuild();
			was_rebuilt = true;
			return true;
		}

		// Check cache version
		size_t pos = 0;
		if (buffer.size < sizeof(DWORD)) {
			// Invalid or old cache format
			rebuild();
			was_rebuilt = true;
			return true;
		}

		DWORD version = 0;
		memcpy(&version, buffer.data + pos, sizeof(DWORD));
		pos += sizeof(DWORD);

		if (version != CACHE_VERSION) {
			// Cache format changed, rebuild
			rebuild();
			was_rebuilt = true;
			return true;
		}

		// Load items
		for (; pos < buffer.size; ) {
			Item item;
			item.unserialize(buffer, pos);
			items.push_back(item);
		}
		last_modified = Util::get_modified(cache_path);

		if (is_outdated()) {
			rebuild();
			was_rebuilt = true;
		}

		return true;
	}

private:
	String      cache_path;
	Time        last_modified;
	StringList  scanned_items;
	Time        scanned_last_modified;

	bool rebuild() {
		Buffer buffer;
		items.clear();

		// Write cache version first
		buffer.load(&CACHE_VERSION, sizeof(CACHE_VERSION));

		Item item;
		item.create(Util::rtrim(path(), DIR_SEP), path());
		item.serialize(buffer);
		items.push_back(item);
		for (size_t i = 0; i < scanned_items.size(); i++) {
			String file_name = scanned_items[i];
			item.create(file_name, path(file_name));
			item.serialize(buffer);
			items.push_back(item);
		}

		save(buffer);
		return true;
	}
	void save(Buffer buffer) {
		::DeleteFile(cache_path.c_str());
		buffer.save(cache_path);
		::SetFileAttributes(cache_path.c_str(), FILE_ATTRIBUTE_HIDDEN);
	}
	void update_max_modified(const String& filename) {
		Time ft = Util::get_modified(path(filename));
		scanned_last_modified = scanned_last_modified < ft ? ft : scanned_last_modified;
	}
	bool is_outdated() {
		if (scanned_last_modified > last_modified || items.size() < 1 || scanned_items.size() + 1 != items.size()) {
			return true;
		}
		for (size_t i = 0; i < scanned_items.size(); i++) if (scanned_items[i] != items[i + 1].name) {
			return true;
		}
		return false;
	}
};

struct MenuEntry {
	Cache::Item* item;     // points to cache item (folder or file)
	String text;           // display text (trimmed)
	bool is_submenu;
	bool populated;        // for lazy submenus
	String submenu_prefix; // relative prefix like L\"Foo.submenu\\\\\"
	bool is_path = false;
};

/**************************************************************************************************
 * DPI-aware icon cache for owner-draw
 **************************************************************************************************/
struct IconCache {
	struct Entry { HBITMAP bmp; SIZE sz; };
	std::unordered_map<const void*, Entry> map;

	Entry& get(HWND hwnd, HBITMAP src) {
		auto it = map.find(src);
		if (it != map.end()) return it->second;

		UINT dpi = GetDpiForWindow(hwnd);
		int s = MulDiv(16, dpi, 96);
		HBITMAP scaled = (HBITMAP)CopyImage(src, IMAGE_BITMAP, s, s, LR_CREATEDIBSECTION);
		return map[src] = { scaled, {s, s} };
	}

	~IconCache() {
		for (auto& kv : map) DeleteObject(kv.second.bmp);
	}
};

/**************************************************************************************************
 * Lazy submenu payload
 **************************************************************************************************/
struct LazySubmenuData {
	Cache* cache;
	String folder_path;
};

/**************************************************************************************************
 * The app
 **************************************************************************************************/
struct App {

	App(Cache* c, const String& options) : cache(c), window(0) {
		hide_header = options.find(L"--hide-header") != String::npos;
		compact_header = options.find(L"--compact-header") != String::npos;
		dark_mode = options.find(L"--dark-mode") != String::npos;
	}

	bool init() {
		// Create window
		Util::kill_other_stackies();
		WNDCLASS wc{0};
		wc.lpfnWndProc = window_proc;
		wc.hInstance = GetModuleHandle(nullptr);
		wc.lpszClassName = STACKY_WINDOW_NAME;
		RegisterClass(&wc);

		window = CreateWindow(STACKY_WINDOW_NAME, STACKY_WINDOW_NAME,
			WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, wc.hInstance, this);

		HMENU menu = CreatePopupMenu();
		build_root_menu(menu);

		POINT pt; GetCursorPos(&pt);
		
		SetForegroundWindow(window);
		TrackPopupMenuEx(menu, TPM_LEFTBUTTON, pt.x, pt.y, window, nullptr);
		return true;
	}

	void run() {
		MSG msg;
		while (GetMessage(&msg, nullptr, 0, 0)) DispatchMessage(&msg);
	}

private:
	HWND    window;
	Cache* cache;
	bool    hide_header;
	bool    compact_header;
	bool    dark_mode;
	IconCache   icon_cache;

	// helper: make a display label for the base folder
	const String header_label() {
		if (compact_header) {
			// show the last folder name instead of full path
			String p = cache->base_dir;
			if (!p.empty() && (p.back() == L'\\' || p.back() == L'/')) p.pop_back();
			size_t pos = p.find_last_of(L"\\/");
			return (pos == String::npos) ? p : p.substr(pos + 1);
		}
		else {
			String p = cache->base_dir;
			if (!p.empty() && p.back() == L'\\')
				p.pop_back();
			return p;
		}
	}

	static void InsertSeparator(HMENU menu) {
		MENUITEMINFO mii{ sizeof(mii) };
		mii.fMask = MIIM_FTYPE | MIIM_DATA;
		mii.fType = MFT_OWNERDRAW;
		mii.dwItemData = (ULONG_PTR)nullptr;   // null itemData = separator marker
		InsertMenuItem(menu, -1, TRUE, &mii);
	}

	static bool IsSeparatorFile(String name) {
		// handle ".separator" and ".separator.lnk"
		if (Util::ends_with(name, L".lnk")) name = Util::rtrim(name, L".lnk");
		return Util::ends_with(name, L".separator");
	}

	void build_root_menu(HMENU menu) {
		if (!hide_header && cache->items.size() >= 1) {
			auto* e = new MenuEntry{};
			e->item = &cache->items[0];      // base folder cache item
			e->is_submenu = false;
			e->populated = false;
			e->is_path = true;
			e->text = header_label();

			MENUITEMINFO mii{ sizeof(mii) };
			mii.fMask = MIIM_FTYPE | MIIM_DATA | MIIM_STRING | MIIM_ID;
			mii.fType = MFT_OWNERDRAW;
			mii.dwItemData = (ULONG_PTR)e;
			mii.dwTypeData = (LPWSTR)e->text.c_str();
			mii.wID = WM_OPEN_TARGET_FOLDER;  // special command

			InsertMenuItem(menu, -1, TRUE, &mii);

			// separator below the folder item (matches old behavior)
			InsertSeparator(menu);
		}

		for (size_t i = 1; i < cache->items.size(); ++i) {
			auto& it = cache->items[i];

			// root: only direct children
			if (it.name.find(DIR_SEP) != String::npos) continue;

			if (IsSeparatorFile(it.name)) {
				InsertSeparator(menu);
				continue;
			}

			// create MenuEntry once; never store mixed pointer types
			auto* e = new MenuEntry{};
			e->item = &it;
			e->is_submenu = it.is_submenu;
			e->populated = false;

			// display text
			if (it.is_submenu) {
				String t = it.name;
				t = Util::rtrim(t, SUBMENU_SUFFIX);
				e->text = t;
				e->submenu_prefix = it.name + DIR_SEP; // RELATIVE prefix!
			}
			else {
				String t = it.name;
				t = Util::rtrim(t, L".bat");
				t = Util::rtrim(t, L".cmd");
				t = Util::rtrim(t, L".exe");
				t = Util::rtrim(t, L".lnk");
				t = Util::rtrim(t, L".url");
				t = Util::rtrim(t, L".vbs");
				e->text = t;
			}

			MENUITEMINFO mii{ sizeof(mii) };
			mii.fMask = MIIM_FTYPE | MIIM_DATA | MIIM_STRING | (it.is_submenu ? MIIM_SUBMENU : MIIM_ID);
			mii.fType = MFT_OWNERDRAW;
			mii.dwItemData = (ULONG_PTR)e;
			mii.dwTypeData = (LPWSTR)e->text.c_str();

			if (it.is_submenu) {
				mii.hSubMenu = CreatePopupMenu();
			}
			else {
				mii.wID = WM_MENU_ITEM + (UINT)i; // unique ID per item
			}

			InsertMenuItem(menu, -1, TRUE, &mii);
		}
	}

	void build_submenu(HMENU menu, const String& prefix) {
		for (size_t i = 0; i < cache->items.size(); ++i) {
			auto& it = cache->items[i];

			// must match relative prefix
			if (it.name.rfind(prefix, 0) != 0) continue;

			String rel = it.name.substr(prefix.size());

			if (IsSeparatorFile(rel)) {
				InsertSeparator(menu);
				continue;
			}

			// direct children only (unless it.is_submenu)
			if (!it.is_submenu && rel.find(DIR_SEP) != String::npos) continue;

			auto* e = new MenuEntry{};
			e->item = &it;
			e->is_submenu = it.is_submenu;
			e->populated = false;

			if (it.is_submenu) {
				// must be direct child submenu folder
				if (rel.find(DIR_SEP) != String::npos) { delete e; continue; }
				e->text = Util::rtrim(rel, SUBMENU_SUFFIX);
				e->submenu_prefix = it.name + DIR_SEP;
			}
			else {
				String t = rel;
				t = Util::rtrim(t, L".lnk");
				t = Util::rtrim(t, L".vbs");
				t = Util::rtrim(t, L".cmd");
				t = Util::rtrim(t, L".bat");
				e->text = t;
			}

			MENUITEMINFO mii{ sizeof(mii) };
			mii.fMask = MIIM_FTYPE | MIIM_DATA | MIIM_STRING | (it.is_submenu ? MIIM_SUBMENU : MIIM_ID);
			mii.fType = MFT_OWNERDRAW;
			mii.dwItemData = (ULONG_PTR)e;
			mii.dwTypeData = (LPWSTR)e->text.c_str();

			if (it.is_submenu) mii.hSubMenu = CreatePopupMenu();
			else mii.wID = WM_MENU_ITEM + (UINT)i;

			InsertMenuItem(menu, -1, TRUE, &mii);
		}
	}

	void on_init_menu_popup(HMENU hMenu) {
		int c = GetMenuItemCount(hMenu);
		for (int i = 0; i < c; ++i) {
			MENUITEMINFO mii{ sizeof(mii) };
			mii.fMask = MIIM_DATA | MIIM_SUBMENU;
			GetMenuItemInfo(hMenu, i, TRUE, &mii);

			if (!mii.hSubMenu) continue;

			auto* e = (MenuEntry*)mii.dwItemData;
			if (!e || !e->is_submenu || e->populated) continue;

			build_submenu(mii.hSubMenu, e->submenu_prefix);
			e->populated = true;
		}
	}

	void on_measure_item(MEASUREITEMSTRUCT* mis) {
		if (mis->CtlType != ODT_MENU) return;

		if (mis->itemData == 0) {
			UINT dpi = GetDpiForWindow(window);
			mis->itemHeight = MulDiv(6, dpi, 96);   // slim separator
			mis->itemWidth = 10;
			return;
		}

		auto* e = (MenuEntry*)mis->itemData;
		if (!e) return;

		UINT dpi = GetDpiForWindow(window);
		int icon = MulDiv(16, dpi, 96);
		int pad = MulDiv(12, dpi, 96);

		HDC hdc = GetDC(window);
		HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
		HFONT old = (HFONT)SelectObject(hdc, font);

		SIZE ts{};
		GetTextExtentPoint32(hdc, e->text.c_str(), (int)e->text.size(), &ts);

		SelectObject(hdc, old);
		ReleaseDC(window, hdc);

		// cap text width for path entries (smart ellipsis will be used when drawing)
		int maxText = ts.cx;
		if (e->is_path) {
			// cap to ~70% of work area width on the monitor where the cursor is
			HMONITOR mon = Util::GetMonitorFromCursor();
			RECT wa = Util::GetWorkAreaForMonitor(mon);
			int maxMenu = (int)((wa.right - wa.left) * 0.70);
			maxText = min(ts.cx, maxMenu);
		}

		mis->itemHeight = max((UINT)GetSystemMetrics(SM_CYMENU), (UINT)(icon + pad / 2));
		mis->itemWidth = icon + pad + maxText + pad;
	}

	void on_draw_item(DRAWITEMSTRUCT* dis) {
		if (dis->CtlType != ODT_MENU) return;

		auto* e = (MenuEntry*)dis->itemData;
		// ----- SEPARATOR DRAW -----
		if (!e) {
			COLORREF bg = dark_mode ? RGB(32, 32, 32) : GetSysColor(COLOR_MENU);
			COLORREF line = dark_mode ? RGB(70, 70, 70) : GetSysColor(COLOR_3DSHADOW);

			// Fill background
			HBRUSH b = CreateSolidBrush(bg);
			FillRect(dis->hDC, &dis->rcItem, b);
			DeleteObject(b);

			// Full width, small padding
			UINT dpi = GetDpiForWindow(window);
			int pad = MulDiv(2, dpi, 96);

			int left = dis->rcItem.left + pad;
			int right = dis->rcItem.right - pad;
			int y = (dis->rcItem.top + dis->rcItem.bottom) / 2;

			// Draw line
			HPEN pen = CreatePen(PS_SOLID, 1, line);
			HPEN old = (HPEN)SelectObject(dis->hDC, pen);

			MoveToEx(dis->hDC, left, y, nullptr);
			LineTo(dis->hDC, right, y);

			SelectObject(dis->hDC, old);
			DeleteObject(pen);
			return;
		}

		if (!e || !e->item) return;

		const bool sel = (dis->itemState & ODS_SELECTED) != 0;
		const bool disab = (dis->itemState & (ODS_DISABLED | ODS_GRAYED)) != 0;

		// Colors
		const COLORREF bg = dark_mode ? RGB(32, 32, 32) : GetSysColor(COLOR_MENU);
		const COLORREF fg = dark_mode ? RGB(240, 240, 240) : GetSysColor(COLOR_MENUTEXT);
		const COLORREF disfg = dark_mode ? RGB(140, 140, 140) : GetSysColor(COLOR_GRAYTEXT);

		// Selection colors (avoid the bright default blue in dark mode)
		const COLORREF selBg = dark_mode ? RGB(64, 64, 64) : GetSysColor(COLOR_HIGHLIGHT);
		const COLORREF selFg = dark_mode ? RGB(255, 255, 255) : GetSysColor(COLOR_HIGHLIGHTTEXT);

		// Paint background
		HBRUSH hbr = CreateSolidBrush(sel ? selBg : bg);
		FillRect(dis->hDC, &dis->rcItem, hbr);
		DeleteObject(hbr);

		// Icon (DPI-scaled) + alpha blend
		auto& ic = icon_cache.get(window, e->item->bmp.hBmp);

		int x = dis->rcItem.left + 4;
		int y = dis->rcItem.top + (dis->rcItem.bottom - dis->rcItem.top - ic.sz.cy) / 2;

		HDC mem = CreateCompatibleDC(dis->hDC);
		HGDIOBJ old = SelectObject(mem, ic.bmp);

		BLENDFUNCTION bf{};
		bf.BlendOp = AC_SRC_OVER;
		bf.SourceConstantAlpha = disab ? 140 : 255; // slightly dim icons when disabled
		bf.AlphaFormat = AC_SRC_ALPHA;

		AlphaBlend(dis->hDC, x, y, ic.sz.cx, ic.sz.cy, mem, 0, 0, ic.sz.cx, ic.sz.cy, bf);

		SelectObject(mem, old);
		DeleteDC(mem);

		// Text
		RECT tr = dis->rcItem;
		tr.left += ic.sz.cx + 8;

		SetBkMode(dis->hDC, TRANSPARENT);
		SetTextColor(dis->hDC, disab ? disfg : (sel ? selFg : fg));

		UINT flags = DT_SINGLELINE | DT_VCENTER | DT_LEFT;
		if (e->is_path) flags |= DT_PATH_ELLIPSIS;
		else           flags |= DT_END_ELLIPSIS;

		DrawText(dis->hDC, e->text.c_str(), -1, &tr, flags);
	}

	static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
		App* app = (App*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

		switch (msg) {
		case WM_NCCREATE:
			SetWindowLongPtr(hwnd, GWLP_USERDATA,
				(LONG_PTR)((CREATESTRUCT*)lp)->lpCreateParams);
			break;

		case WM_INITMENUPOPUP:
			app->on_init_menu_popup((HMENU)wp);
			break;

		case WM_MEASUREITEM:
			app->on_measure_item((MEASUREITEMSTRUCT*)lp);
			return TRUE;

		case WM_DRAWITEM:
			app->on_draw_item((DRAWITEMSTRUCT*)lp);
			return TRUE;

		case WM_COMMAND: {
			UINT id = LOWORD(wp);
			if (id == WM_OPEN_TARGET_FOLDER) {
				ShellExecute(nullptr, nullptr, app->cache->path().c_str(), nullptr, nullptr, SW_NORMAL);
				return TRUE;
			}

			if (id >= WM_MENU_ITEM) {
				size_t idx = id - WM_MENU_ITEM;
				auto& it = app->cache->items[idx];
				String cmd = app->cache->path(it.name);

				if (GetKeyState(VK_SHIFT) & 0x8000)
				{
					TCHAR  filepath[MAX_PATH] = { 0 };
					Util::ResolveShortcut(NULL, cmd.c_str(), filepath, _countof(filepath));

					ITEMIDLIST* pidl = ILCreateFromPath(filepath);
					if (pidl)
					{
						SHOpenFolderAndSelectItems(pidl, 0, 0, 0);
						ILFree(pidl);
					}
				}
				else
				{
					ShellExecute(nullptr, nullptr, cmd.c_str(), nullptr, nullptr, SW_NORMAL);
				}
			}
			break;
		}
		case WM_EXITMENULOOP:
			// WM_EXITMENULOOP is sent before WM_COMMAND, so the app termination has to be delayed.
			// This also allows to wait for the possible UAC prompt.
			::SetTimer(hwnd, 0, APP_EXIT_DELAY, 0);
			break;

		case WM_TIMER:
			::PostQuitMessage(0);
			::DestroyWindow(hwnd);
			break;
		}
		return DefWindowProc(hwnd, msg, wp, lp);
	}
};

/**************************************************************************************************
 * App entry point
 **************************************************************************************************/
int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPTSTR cmd_line, int) {
	ComInit com;

	String  stack_path, opts;
	int     cmd_line_error = Util::parse_cmd_line(cmd_line, stack_path, opts);
	String  err_title = String(L"Stacky v") + STACKY_VERSION_STR + L": ";
	String  err_msg = L"Path: " + stack_path;

	Cache   cache(stack_path);
	App     app(&cache, opts);

	if (cmd_line_error == ERR_PATH_MISSING) {
		Util::msgt(
			err_title + L"Parameter missing",
			L"Pass path to the stack folder in the command line, for ex.: \n\n"
			L"        stacky.exe D:\\Projects [options]\n\n"
			L"Options:\n"
			L"  --hide-header      Hide the top folder item and separator\n"
			L"  --compact-header   Show only folder name in the header\n"
			L"  --dark-mode        Use dark-mode for the menu"
		);
	}
	else if (cmd_line_error == ERR_PATH_INVALID) {
		Util::msgt(
			err_title + L"Invalid parameter",
			L"Path: %s is not a valid directory",
			stack_path.c_str()
		);
	}
	else if (!cache.scan()) {
		Util::msgt(
			err_title + L"Invalid path",
			L"%s",
			err_msg.c_str()
		);
	}
	else if (!cache.load()) {
		Util::msgt(
			err_title + L"Failed to load stack cache",
			L"%s",
			err_msg.c_str()
		);
	}
	else if (!app.init()) {
		Util::msgt(
			err_title + L"App init failed",
			L"%s",
			err_msg.c_str()
		);
	}
	else
		app.run();

	return 0;
}

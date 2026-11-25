#include "fs.h"
#include <zlib.h>

#if !defined(S_ISREG) && defined(S_IFMT) && defined(S_IFREG)
#define S_ISREG(m) (((m)&S_IFMT) == S_IFREG)
#endif

map<void*, void*> ZFile::s_mapFiles;
static mutex s_mtxMapFiles;

bool ZFile::IsRegularFile(const char* path)
{
	struct stat st = { 0 };
	return 0 == stat(path, &st) && S_ISREG(st.st_mode);
}

void* ZFile::MapFile(const char* path, size_t offset, size_t size, size_t* psize, bool ro)
{
	void* base = NULL;

#ifdef _WIN32

	HANDLE hFile = ::CreateFileA(path, ro ? GENERIC_READ : (GENERIC_READ | GENERIC_WRITE), ro ? FILE_SHARE_READ : (FILE_SHARE_READ | FILE_SHARE_WRITE), NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (INVALID_HANDLE_VALUE != hFile) {
		if (size <= 0) {
			size = ::GetFileSize(hFile, NULL);
		}

		if (NULL != psize) {
			*psize = size;
		}

		HANDLE hMap = ::CreateFileMapping(hFile, NULL, ro ? PAGE_READONLY : PAGE_READWRITE, 0, (DWORD)size, NULL);
		if (NULL != hMap) {
			base = ::MapViewOfFile(hMap, ro ? FILE_MAP_READ : FILE_MAP_ALL_ACCESS, 0, 0, size);
			if (NULL != base) {
				lock_guard<mutex> lock(s_mtxMapFiles);
				s_mapFiles[base] = hMap;
			} else {
				::CloseHandle(hMap);
			}
		}
		::CloseHandle(hFile);
	}

#else

	int fd = open(path, ro ? O_RDONLY : O_RDWR);
	if (fd >= 0) {
		if (size <= 0) {
			struct stat st = { 0 };
			fstat(fd, &st);
			size = st.st_size;
		}

		if (NULL != psize) {
			*psize = size;
		}

		base = mmap(NULL, size, ro ? PROT_READ : PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset);
		if (MAP_FAILED == base) {
			base = NULL;
		}
		close(fd);
	}

#endif

	return base;
}

bool ZFile::UnmapFile(void* base, size_t size)
{
#ifdef _WIN32
	lock_guard<mutex> lock(s_mtxMapFiles);
	auto it = s_mapFiles.find(base);
	if (it != s_mapFiles.end()) {
		::UnmapViewOfFile(base);
		::CloseHandle(it->second);
		s_mapFiles.erase(it);
		return true;
	}
	return false;
#else
	return (0 == munmap(base, size));
#endif
}

bool ZFile::WriteFile(const char* szFile, const char* szData, size_t sLen)
{
	if (NULL == szFile) {
		return false;
	}

	FILE* fp = NULL;
	_fopen64(fp, szFile, "wb");
	if (NULL != fp) {
		size_t written = 0;
		size_t to_write = sLen;
		if (NULL != szData) {
			while (written < to_write) {
				size_t ret = fwrite(szData + written, 1, to_write - written, fp);
				if (ret <= 0) {
					break;
				}
				written += ret;
			}
		}
		fclose(fp);
		return (written == to_write);
	} else {
		ZLog::ErrorV("WriteFile: Failed in fopen! %s, %s\n", szFile, strerror(errno));
	}
	return false;
}

bool ZFile::WriteFile(const char* szFile, const string& strData)
{
	return WriteFile(szFile, strData.data(), strData.size());
}

bool ZFile::WriteFileV(string& strData, const char* szPath, ...)
{
	FORMAT_V(szPath, szRealPath);
	return WriteFile(szRealPath, strData);
}

bool ZFile::WriteFileV(const char* szData, size_t sLen, const char* szPath, ...)
{
	FORMAT_V(szPath, szRealPath);
	return WriteFile(szRealPath, szData, sLen);
}

bool ZFile::ReadFile(const char* szFile, string& strData)
{
	strData.clear();
	FILE* fp = NULL;
	_fopen64(fp, szFile, "rb");
	if (NULL != fp) {
		_fseeki64(fp, 0, SEEK_END);
		int64_t to_read = _ftelli64(fp);
		_fseeki64(fp, 0, SEEK_SET);
		to_read = (to_read > 0 ? to_read : 0);
		strData.resize((size_t)to_read);
		if (strData.capacity() >= (size_t)to_read) {
			int64_t readed = 0;
			while (readed < to_read) {
				size_t ret = fread(&(strData[(size_t)readed]), 1, (size_t)(to_read - readed), fp);
				if (ret <= 0) {
					break;
				}
				readed += ret;
			}
		}
		fclose(fp);
		return (strData.size() == to_read);
	}
	return false;
}

bool ZFile::ReadFileV(string& strData, const char* szPath, ...)
{
	FORMAT_V(szPath, szRealPath);
	return ZFile::ReadFile(szRealPath, strData);
}

bool ZFile::AppendFile(const char* szFile, const char* szData, size_t sLen)
{
	FILE* fp = NULL;
	_fopen64(fp, szFile, "ab+");
	if (NULL != fp) {
		int64_t towrite = sLen;
		while (towrite > 0) {
			int64_t nwrite = fwrite(szData + (sLen - towrite), 1, towrite, fp);
			if (nwrite <= 0) {
				break;
			}
			towrite -= nwrite;
		}

		fclose(fp);
		return (towrite > 0) ? false : true;
	} else {
		ZLog::ErrorV("AppendFile: Failed in fopen! %s, %s\n", szFile, strerror(errno));
	}
	return false;
}

bool ZFile::AppendFile(const char* szFile, const string& strData)
{
	return AppendFile(szFile, strData.data(), strData.size());
}

bool ZFile::IsFolder(const char* szFolder)
{
#ifdef _WIN32
	return ::PathIsDirectoryA(szFolder);
#else
	struct stat st = { 0 };
	return 0 == stat(szFolder, &st) && S_ISDIR(st.st_mode);
#endif
}

bool ZFile::IsFolderV(const char* szPath, ...)
{
	FORMAT_V(szPath, szFolder);
	return IsFolder(szFolder);
}

bool ZFile::CreateFolder(const char* szFolder)
{
	string strPath = GetFullPath(szFolder);
	if (!IsFolder(strPath.c_str())) {
#ifdef _WIN32
		int32_t nRet = ::SHCreateDirectoryExA(NULL, strPath.c_str(), NULL);
		if (ERROR_SUCCESS != nRet && ERROR_ALREADY_EXISTS != nRet) {
			return false;
		}
#else
		size_t pos = 0;
		pos = strPath.find('/', pos);
		while (string::npos != pos) {
			string strFolder = strPath.substr(0, pos++);
			if (!strFolder.empty()) {
				if (0 != mkdir(strFolder.c_str(), 0755)) {
					if (EEXIST != errno) {
						return false;
					}
				}
			}
			pos = strPath.find('/', pos);
		}
		if (0 != mkdir(strPath.c_str(), 0755)) {
			if (EEXIST != errno) {
				return false;
			}
		}
		return true;
#endif
	}
	return true;
}

bool ZFile::CreateFolderV(const char* szPath, ...)
{
	FORMAT_V(szPath, szFolder);
	return CreateFolder(szFolder);
}

int ZFile::RemoveFolderCallBack(const char* fpath, const struct stat* sb, int typeflag, struct FTW* ftwbuf)
{
	int ret = remove(fpath);
	if (ret) {
		perror(fpath);
	}
	return ret;
}

bool ZFile::RemoveFolder(const char* szFolder)
{
	if (!IsFolder(szFolder)) {
		RemoveFile(szFolder);
		return true;
	}
#ifdef _WIN32
	string strFolder = szFolder;
	strFolder.push_back('\0');

	SHFILEOPSTRUCTA shfs;
	memset(&shfs, 0, sizeof(shfs));
	shfs.wFunc = FO_DELETE;
	shfs.pFrom = strFolder.c_str();
	shfs.fFlags = FOF_NOCONFIRMATION;
	shfs.fFlags |= (FOF_SILENT | FOF_NOERRORUI);
	return (0 == ::SHFileOperationA(&shfs));
#else
	return nftw(szFolder, RemoveFolderCallBack, 64, FTW_DEPTH | FTW_PHYS);
#endif
}

bool ZFile::RemoveFolderV(const char* szPath, ...)
{
	FORMAT_V(szPath, szFolder);
	return RemoveFolder(szFolder);
}

bool ZFile::RemoveFile(const char* szFile)
{
	return (0 == remove(szFile));
}

bool ZFile::RemoveFileV(const char* szPath, ...)
{
	FORMAT_V(szPath, szFile);
	return RemoveFile(szFile);
}

bool ZFile::IsFileExists(const char* szFile)
{
	if (NULL == szFile) {
		return false;
	}

#ifdef _WIN32
	return ::PathFileExistsA(szFile);
#else
	return (0 == access(szFile, F_OK));
#endif
}

bool ZFile::IsFileExistsV(const char* szPath, ...)
{
	FORMAT_V(szPath, szFile);
	return IsFileExists(szFile);
}

bool ZFile::IsZipFile(const char* szFile)
{
	if (NULL != szFile && !IsFolder(szFile)) {
		FILE* fp = NULL;
		_fopen64(fp, szFile, "rb");
		if (NULL != fp) {
			uint8_t buf[2] = { 0 };
			fread(buf, 1, 2, fp);
			fclose(fp);
			return (0 == memcmp("PK", buf, 2));
		}
	}
	return false;
}

bool ZFile::CopyFile(const char* szSrcFile, const char* szDestFile)
{
#ifdef _WIN32
	return ::CopyFileA(szSrcFile, szDestFile, FALSE) ? true : false;
#else 

	int src_id = -1;
	int dest_fd = -1;
	ssize_t sum_readed = 0;
	ssize_t sum_written = 0;
	src_id = open(szSrcFile, O_RDONLY);
	if (-1 != src_id) {
		dest_fd = open(szDestFile, O_CREAT | O_WRONLY | O_TRUNC, 0644);
		if (-1 != dest_fd) {
			char buffer[65536];
			ssize_t bytes_read = read(src_id, buffer, sizeof(buffer));
			while (bytes_read > 0) {
				sum_readed += bytes_read;
				ssize_t bytes_written = write(dest_fd, buffer, bytes_read);
				if (bytes_written != bytes_read) {
					break;
				}
				sum_written += bytes_written;
				bytes_read = read(src_id, buffer, sizeof(buffer));
			}
			close(dest_fd);
		}
		close(src_id);
	}

	if (-1 == src_id || -1 == dest_fd || sum_readed != sum_written) {
		return false;
	}

	return true;

#endif
}

bool ZFile::CopyFileV(const char* szSrcFile, const char* szDestPath, ...)
{
	FORMAT_V(szDestPath, szDestFile);
	return CopyFile(szSrcFile, szDestFile);
}

string ZFile::GetFullPath(const char* szPath)
{
	string strPath = szPath;
	if (!strPath.empty()) {
#ifdef _WIN32
		char path[PATH_MAX] = { 0 };
		if (NULL != _fullpath(path, szPath, PATH_MAX)) {
			strPath = path;
		}
		ZUtil::StringReplace(strPath, "/", "\\");
#else
		char path[PATH_MAX] = { 0 };
		if (NULL != realpath(szPath, path)) {
			strPath = path;
		}
		ZUtil::StringReplace(strPath, "//", "/");
#endif
	}
	return strPath;
}

string ZFile::GetRealPathV(const char* szPath, ...)
{
	FORMAT_V(szPath, szRealPath);
	return GetFullPath(szRealPath);
}

int64_t ZFile::GetFileSize(FILE* fp)
{
	if (NULL != fp) {
		_fseeki64(fp, 0, SEEK_END);
		int64_t size = _ftelli64(fp);
		_fseeki64(fp, 0, SEEK_SET);
		return (size > 0 ? size : 0);
	}
	return 0;
}

int64_t ZFile::GetFileSize(const char* szPath)
{
#ifdef _WIN32
	int64_t size = 0;
	FILE* fp = NULL;
	_fopen64(fp, szPath, "rb");
	if (NULL != fp) {
		size = GetFileSize(fp);
		fclose(fp);
	}
	return size;
#else
	struct stat st;
	return (0 == stat(szPath, &st) && S_ISREG(st.st_mode)) ? (int64_t)st.st_size : 0;
#endif
}

int64_t ZFile::GetFileSizeV(const char* szPath, ...)
{
	FORMAT_V(szPath, szFile);
	return GetFileSize(szFile);
}

string ZFile::GetFileSizeString(const char* szFile)
{
	return  ZUtil::FormatSize(GetFileSize(szFile), 1024);
}

bool ZFile::IsPathSuffix(const string& strPath, const char* suffix)
{
	size_t nPos = strPath.rfind(suffix);
	if (string::npos != nPos) {
		if (nPos == (strPath.size() - strlen(suffix))) {
			return true;
		}
	}
	return false;
}

const char* ZFile::GetTempFolder()
{
	static once_flag s_flag;
	static string s_strTempFolder;
	if (s_strTempFolder.empty()) {
		call_once(s_flag, [&]() {
#ifdef _WIN32
			char szTempPath[PATH_MAX] = { 0 };
			if (0 != ::GetTempPathA(PATH_MAX, szTempPath)) {
				s_strTempFolder = szTempPath;
			} else {
				s_strTempFolder = GetFullPath("./");
			}
			if (!s_strTempFolder.empty() && s_strTempFolder.back() == '\\') {
				s_strTempFolder.pop_back();
			}
#else
			s_strTempFolder = "/tmp";
#endif
		});
	}
	return s_strTempFolder.c_str();
}

bool ZFile::EnumFolder(const char* szFolder, bool bRecursive, enum_folder_callback filter, enum_folder_callback callback)
{
	string strFolder = szFolder;
	if (strFolder.empty() || NULL == callback) {
		return false;
	}

#ifdef _WIN32
	string strFromFolder = strFolder + "\\*";
	WIN32_FIND_DATAA fd = { 0 };
	HANDLE hFind = ::FindFirstFileA(strFromFolder.c_str(), &fd);
	if (INVALID_HANDLE_VALUE == hFind) {
		return false;
	}

	while (::FindNextFileA(hFind, &fd)) {
		if (0 == strcmp(fd.cFileName, ".") || 0 == strcmp(fd.cFileName, "..")) {
			continue;
		}

		string strName = fd.cFileName;
		string strPath = strFolder + "\\" + strName;
		bool bFolder = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? true : false;

		if (NULL != filter) {
			if (filter(bFolder, strPath)) {
				continue;
			}
		}

		if (callback(bFolder, strPath)) {
			break;
		}

		if (bFolder && bRecursive) {
			EnumFolder(strPath.c_str(), bRecursive, filter, callback);
		}
	}
	::FindClose(hFind);

#else

	DIR* dir = opendir(szFolder);
	if (NULL == dir) {
		return false;
	}
	
	dirent* ptr = readdir(dir);
	while (NULL != ptr) {
		if (0 == strcmp(ptr->d_name, ".") || 0 == strcmp(ptr->d_name, "..")) {
			ptr = readdir(dir);
			continue;
		}

		string strPath = strFolder + "/" + ptr->d_name;

		bool bFolder = false;
		if (DT_DIR == ptr->d_type) {
			bFolder = true;
		} else if (DT_UNKNOWN == ptr->d_type) {
			struct stat st = { 0 };
			if (0 == stat(strPath.c_str(), &st) && S_ISDIR(st.st_mode)) {
				bFolder = true;
			}
		}

		if (NULL != filter) {
			if (filter(bFolder, strPath)) {
				ptr = readdir(dir);
				continue;
			}
		}

		if (callback(bFolder, strPath)) {
			break;
		}

		if (bFolder && bRecursive) {
			EnumFolder(strPath.c_str(), bRecursive, filter, callback);
		}

		ptr = readdir(dir);
	}
	closedir(dir);

#endif

	return true;
}

bool ZFile::PathRemoveFileSpec(string& path)
{
	size_t pos = path.find_last_of("/\\");
	if (pos != std::string::npos) {
		path = path.substr(0, pos);
		return true;
	}
	return false;
}

#ifndef _WIN32
struct ZZipEntry
{
	string strName;
	uint16_t uMethod;
	uint32_t uCRC;
	uint64_t uCompSize;
	uint64_t uSize;
	uint64_t uOffset;
	uint32_t uMode;
};

static uint16_t ZipLE16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t ZipLE32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint64_t ZipLE64(const uint8_t* p) { return ZipLE32(p) | ((uint64_t)ZipLE32(p + 4) << 32); }

static bool ZipReadAt(int fd, void* pBuf, size_t uLen, uint64_t uOffset)
{
	uint8_t* p = (uint8_t*)pBuf;
	while (uLen > 0) {
		ssize_t n = pread(fd, p, uLen, (off_t)uOffset);
		if (n < 0 && EINTR == errno) {
			continue;
		}
		if (n <= 0) {
			return false;
		}
		p += n;
		uLen -= n;
		uOffset += n;
	}
	return true;
}

static bool ZipWriteAll(int fd, const uint8_t* p, size_t uLen)
{
	while (uLen > 0) {
		ssize_t n = write(fd, p, uLen);
		if (n < 0 && EINTR == errno) {
			continue;
		}
		if (n <= 0) {
			return false;
		}
		p += n;
		uLen -= n;
	}
	return true;
}

static bool ZipExtractEntry(int fd, const ZZipEntry& ze, const string& strPath)
{
	uint8_t lh[30];
	if (!ZipReadAt(fd, lh, sizeof(lh), ze.uOffset) || 0x04034b50 != ZipLE32(lh)) {
		return false;
	}
	uint64_t uData = ze.uOffset + sizeof(lh) + ZipLE16(lh + 26) + ZipLE16(lh + 28);

	int out = open(strPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (out < 0) {
		return false;
	}

	const uint64_t uChunk = 1 << 20;
	vector<uint8_t> in(min(uChunk, max<uint64_t>(ze.uCompSize, 1)));
	uLong uCRC = crc32(0, NULL, 0);
	uint64_t uWritten = 0;
	bool bOK = true;

	if (0 == ze.uSize) {
	} else if (0 == ze.uMethod) {
		bOK = (ze.uCompSize == ze.uSize);
		while (bOK && uWritten < ze.uSize) {
			size_t n = (size_t)min<uint64_t>(in.size(), ze.uSize - uWritten);
			bOK = ZipReadAt(fd, in.data(), n, uData + uWritten) && ZipWriteAll(out, in.data(), n);
			uCRC = crc32(uCRC, in.data(), (uInt)n);
			uWritten += n;
		}
	} else {
		vector<uint8_t> buf(min(uChunk, ze.uSize));
		z_stream zs;
		memset(&zs, 0, sizeof(zs));
		bOK = (Z_OK == inflateInit2(&zs, -MAX_WBITS));
		uint64_t uRead = 0;
		int ret = Z_OK;
		while (bOK && Z_STREAM_END != ret) {
			if (0 == zs.avail_in && uRead < ze.uCompSize) {
				size_t n = (size_t)min<uint64_t>(in.size(), ze.uCompSize - uRead);
				if (!ZipReadAt(fd, in.data(), n, uData + uRead)) {
					bOK = false;
					break;
				}
				uRead += n;
				zs.next_in = in.data();
				zs.avail_in = (uInt)n;
			}
			zs.next_out = buf.data();
			zs.avail_out = (uInt)buf.size();
			ret = inflate(&zs, Z_NO_FLUSH);
			size_t n = buf.size() - zs.avail_out;
			uWritten += n;
			bOK = (Z_OK == ret || Z_STREAM_END == ret) && uWritten <= ze.uSize && ZipWriteAll(out, buf.data(), n);
			uCRC = crc32(uCRC, buf.data(), (uInt)n);
		}
		inflateEnd(&zs);
	}

	if (ze.uMode) {
		fchmod(out, ze.uMode);
	}
	bOK = (0 == close(out)) && bOK;
	return bOK && uWritten == ze.uSize && uCRC == ze.uCRC;
}

static bool ZipWriteAt(int fd, const uint8_t* p, size_t uLen, uint64_t uOffset)
{
	while (uLen > 0) {
		ssize_t n = pwrite(fd, p, uLen, (off_t)uOffset);
		if (n < 0 && EINTR == errno) {
			continue;
		}
		if (n <= 0) {
			return false;
		}
		p += n;
		uLen -= n;
		uOffset += n;
	}
	return true;
}

struct ZZipItem
{
	string strPath;
	string strName;
	uint64_t uSize;
	uint64_t uOffset;
	uint32_t uCRC;
	uint32_t uMode;
	uint16_t uTime;
	uint16_t uDate;
};

static void ZipPut16(string& s, uint16_t v) { s.push_back((char)(v & 0xFF)); s.push_back((char)(v >> 8)); }
static void ZipPut32(string& s, uint32_t v) { ZipPut16(s, (uint16_t)v); ZipPut16(s, (uint16_t)(v >> 16)); }
static void ZipPut64(string& s, uint64_t v) { ZipPut32(s, (uint32_t)v); ZipPut32(s, (uint32_t)(v >> 32)); }

static uint64_t ZipLocalHeaderSize(const ZZipItem& zi)
{
	return 30 + zi.strName.size() + (zi.uSize >= 0xFFFFFFFF ? 20 : 0);
}

static string ZipLocalHeader(const ZZipItem& zi)
{
	bool bZip64 = (zi.uSize >= 0xFFFFFFFF);
	uint32_t uSize32 = bZip64 ? 0xFFFFFFFF : (uint32_t)zi.uSize;
	string s;
	ZipPut32(s, 0x04034b50);
	ZipPut16(s, bZip64 ? 45 : 10);
	ZipPut16(s, 1 << 11);
	ZipPut16(s, 0);
	ZipPut16(s, zi.uTime);
	ZipPut16(s, zi.uDate);
	ZipPut32(s, zi.uCRC);
	ZipPut32(s, uSize32);
	ZipPut32(s, uSize32);
	ZipPut16(s, (uint16_t)zi.strName.size());
	ZipPut16(s, bZip64 ? 20 : 0);
	s += zi.strName;
	if (bZip64) {
		ZipPut16(s, 0x0001);
		ZipPut16(s, 16);
		ZipPut64(s, zi.uSize);
		ZipPut64(s, zi.uSize);
	}
	return s;
}

static void ZipCentralHeader(string& s, const ZZipItem& zi)
{
	string strExtra;
	if (zi.uSize >= 0xFFFFFFFF) {
		ZipPut64(strExtra, zi.uSize);
		ZipPut64(strExtra, zi.uSize);
	}
	if (zi.uOffset >= 0xFFFFFFFF) {
		ZipPut64(strExtra, zi.uOffset);
	}
	uint32_t uSize32 = (zi.uSize >= 0xFFFFFFFF) ? 0xFFFFFFFF : (uint32_t)zi.uSize;
	ZipPut32(s, 0x02014b50);
	ZipPut16(s, (3 << 8) | 45);
	ZipPut16(s, strExtra.empty() ? 10 : 45);
	ZipPut16(s, 1 << 11);
	ZipPut16(s, 0);
	ZipPut16(s, zi.uTime);
	ZipPut16(s, zi.uDate);
	ZipPut32(s, zi.uCRC);
	ZipPut32(s, uSize32);
	ZipPut32(s, uSize32);
	ZipPut16(s, (uint16_t)zi.strName.size());
	ZipPut16(s, strExtra.empty() ? 0 : (uint16_t)(4 + strExtra.size()));
	ZipPut16(s, 0);
	ZipPut16(s, 0);
	ZipPut16(s, 0);
	ZipPut32(s, (S_IFREG | zi.uMode) << 16);
	ZipPut32(s, (zi.uOffset >= 0xFFFFFFFF) ? 0xFFFFFFFF : (uint32_t)zi.uOffset);
	s += zi.strName;
	if (!strExtra.empty()) {
		ZipPut16(s, 0x0001);
		ZipPut16(s, (uint16_t)strExtra.size());
		s += strExtra;
	}
}

static bool ZipStoreItem(int out, ZZipItem& zi)
{
	int in = open(zi.strPath.c_str(), O_RDONLY);
	if (in < 0) {
		return false;
	}
	vector<uint8_t> buf((size_t)min<uint64_t>(1 << 20, max<uint64_t>(zi.uSize, 1)));
	uLong uCRC = crc32(0, NULL, 0);
	uint64_t uData = zi.uOffset + ZipLocalHeaderSize(zi);
	uint64_t uDone = 0;
	bool bOK = true;
	while (bOK && uDone < zi.uSize) {
		ssize_t n = read(in, buf.data(), (size_t)min<uint64_t>(buf.size(), zi.uSize - uDone));
		if (n < 0 && EINTR == errno) {
			continue;
		}
		bOK = (n > 0) && ZipWriteAt(out, buf.data(), n, uData + uDone);
		if (bOK) {
			uCRC = crc32(uCRC, buf.data(), (uInt)n);
			uDone += n;
		}
	}
	close(in);
	zi.uCRC = (uint32_t)uCRC;
	string strHeader = ZipLocalHeader(zi);
	return bOK && ZipWriteAt(out, (const uint8_t*)strHeader.data(), strHeader.size(), zi.uOffset);
}

static bool ZipArchive(int out, const string& strFolder, const string& strPrefix, const function<void(double)>& fnProgress)
{
	vector<ZZipItem> arrItems;
	bool bEnumOK = true;
	ZFile::EnumFolder(strFolder.c_str(), true, NULL, [&](bool bFolder, const string& strPath) {
		if (bFolder) {
			return false;
		}
		struct stat st = { 0 };
		if (0 != stat(strPath.c_str(), &st)) {
			bEnumOK = false;
			return true;
		}
		if (!S_ISREG(st.st_mode)) {
			return false;
		}
		ZZipItem zi;
		zi.strPath = strPath;
		zi.strName = strPrefix + strPath.substr(strFolder.size() + 1);
		zi.uSize = st.st_size;
		zi.uOffset = 0;
		zi.uCRC = 0;
		zi.uMode = st.st_mode & 0777;
		struct tm tm = { 0 };
		localtime_r(&st.st_mtime, &tm);
		zi.uTime = (uint16_t)((tm.tm_hour << 11) | (tm.tm_min << 5) | (tm.tm_sec / 2));
		zi.uDate = (uint16_t)((max(tm.tm_year - 80, 0) << 9) | ((tm.tm_mon + 1) << 5) | tm.tm_mday);
		if (zi.strName.size() > 0xFFFF) {
			bEnumOK = false;
			return true;
		}
		arrItems.push_back(zi);
		return false;
	});
	if (!bEnumOK) {
		return false;
	}

	uint64_t uOffset = 0;
	uint64_t uTotal = 0;
	vector<size_t> arrOrder;
	for (size_t i = 0; i < arrItems.size(); i++) {
		arrItems[i].uOffset = uOffset;
		uOffset += ZipLocalHeaderSize(arrItems[i]) + arrItems[i].uSize;
		uTotal += arrItems[i].uSize;
		arrOrder.push_back(i);
	}
	sort(arrOrder.begin(), arrOrder.end(), [&](size_t a, size_t b) {
		return arrItems[a].uSize > arrItems[b].uSize;
	});

	atomic<bool> bFailed(false);
	mutex mtxProgress;
	uint64_t uDone = 0;
	double dReported = 0;
	ZUtil::ParallelFor(arrOrder.size(), [&](size_t k) {
		ZZipItem& zi = arrItems[arrOrder[k]];
		if (bFailed || !ZipStoreItem(out, zi)) {
			if (!bFailed.exchange(true)) {
				ZLog::ErrorV(">>> Failed to archive: %s\n", zi.strPath.c_str());
			}
			return;
		}
		if (fnProgress && uTotal > 0) {
			lock_guard<mutex> lock(mtxProgress);
			uDone += zi.uSize;
			double dProgress = (double)uDone / uTotal;
			if (dProgress - dReported >= 0.01) {
				dReported = dProgress;
				fnProgress(dProgress);
			}
		}
	});
	if (bFailed) {
		return false;
	}

	string strTail;
	for (const ZZipItem& zi : arrItems) {
		ZipCentralHeader(strTail, zi);
	}
	uint64_t uCount = arrItems.size();
	uint64_t uCDSize = strTail.size();
	bool bZip64 = (uCount >= 0xFFFF || uOffset >= 0xFFFFFFFF || uCDSize >= 0xFFFFFFFF);
	if (bZip64) {
		ZipPut32(strTail, 0x06064b50);
		ZipPut64(strTail, 44);
		ZipPut16(strTail, (3 << 8) | 45);
		ZipPut16(strTail, 45);
		ZipPut32(strTail, 0);
		ZipPut32(strTail, 0);
		ZipPut64(strTail, uCount);
		ZipPut64(strTail, uCount);
		ZipPut64(strTail, uCDSize);
		ZipPut64(strTail, uOffset);
		ZipPut32(strTail, 0x07064b50);
		ZipPut32(strTail, 0);
		ZipPut64(strTail, uOffset + uCDSize);
		ZipPut32(strTail, 1);
	}
	ZipPut32(strTail, 0x06054b50);
	ZipPut16(strTail, 0);
	ZipPut16(strTail, 0);
	ZipPut16(strTail, bZip64 ? 0xFFFF : (uint16_t)uCount);
	ZipPut16(strTail, bZip64 ? 0xFFFF : (uint16_t)uCount);
	ZipPut32(strTail, bZip64 ? 0xFFFFFFFF : (uint32_t)uCDSize);
	ZipPut32(strTail, bZip64 ? 0xFFFFFFFF : (uint32_t)uOffset);
	ZipPut16(strTail, 0);
	if (!ZipWriteAt(out, (const uint8_t*)strTail.data(), strTail.size(), uOffset)) {
		return false;
	}
	if (fnProgress) {
		fnProgress(1.0);
	}
	return true;
}

static int ZipExtract(int fd, const string& strFolder, const function<void(double)>& fnProgress)
{
	struct stat st = { 0 };
	if (0 != fstat(fd, &st)) {
		return -1;
	}
	uint64_t uFileSize = st.st_size;

	size_t uTail = (size_t)min<uint64_t>(uFileSize, 22 + 65535);
	vector<uint8_t> tail(uTail);
	if (uTail < 22 || !ZipReadAt(fd, tail.data(), uTail, uFileSize - uTail)) {
		return -1;
	}
	size_t uEOCD = uTail;
	for (size_t i = uTail - 22 + 1; i-- > 0;) {
		if (0x06054b50 == ZipLE32(&tail[i])) {
			uEOCD = i;
			break;
		}
	}
	if (uEOCD == uTail) {
		return -1;
	}

	uint64_t uCount = ZipLE16(&tail[uEOCD + 10]);
	uint64_t uCDSize = ZipLE32(&tail[uEOCD + 12]);
	uint64_t uCDOffset = ZipLE32(&tail[uEOCD + 16]);
	if (0xFFFF == uCount || 0xFFFFFFFF == uCDSize || 0xFFFFFFFF == uCDOffset) {
		uint64_t uEOCDPos = uFileSize - uTail + uEOCD;
		uint8_t loc[20];
		uint8_t z64[56];
		if (uEOCDPos < sizeof(loc) || !ZipReadAt(fd, loc, sizeof(loc), uEOCDPos - sizeof(loc)) || 0x07064b50 != ZipLE32(loc) ||
			!ZipReadAt(fd, z64, sizeof(z64), ZipLE64(loc + 8)) || 0x06064b50 != ZipLE32(z64)) {
			return -1;
		}
		uCount = ZipLE64(z64 + 32);
		uCDSize = ZipLE64(z64 + 40);
		uCDOffset = ZipLE64(z64 + 48);
	}
	if (uCDSize > uFileSize || uCDOffset > uFileSize - uCDSize) {
		return -1;
	}

	vector<uint8_t> cd(uCDSize);
	if (!ZipReadAt(fd, cd.data(), cd.size(), uCDOffset)) {
		return -1;
	}

	vector<ZZipEntry> arrEntries;
	map<string, size_t> mapLast;
	set<string> setFolders;
	bool bUnsupported = false;
	size_t p = 0;
	for (uint64_t i = 0; i < uCount; i++) {
		if (p + 46 > cd.size() || 0x02014b50 != ZipLE32(&cd[p])) {
			return -1;
		}
		const uint8_t* h = &cd[p];
		uint16_t uFlags = ZipLE16(h + 8);
		uint16_t uNameLen = ZipLE16(h + 28);
		uint16_t uExtraLen = ZipLE16(h + 30);
		size_t uNext = p + 46 + uNameLen + uExtraLen + ZipLE16(h + 32);
		if (uNext > cd.size()) {
			return -1;
		}

		ZZipEntry ze;
		ze.strName.assign((const char*)h + 46, uNameLen);
		ze.uMethod = ZipLE16(h + 10);
		ze.uCRC = ZipLE32(h + 16);
		ze.uCompSize = ZipLE32(h + 20);
		ze.uSize = ZipLE32(h + 24);
		ze.uOffset = ZipLE32(h + 42);
		ze.uMode = (ZipLE32(h + 38) >> 16) & 0x1FF;
		if (ze.uMode < 0400) {
			ze.uMode = 0;
		}

		for (const uint8_t *x = h + 46 + uNameLen, *xEnd = x + uExtraLen; x + 4 <= xEnd;) {
			uint16_t uID = ZipLE16(x);
			const uint8_t* v = x + 4;
			const uint8_t* vEnd = v + ZipLE16(x + 2);
			if (vEnd > xEnd) {
				break;
			}
			if (0x0001 == uID) {
				uint64_t* arrFields[] = { &ze.uSize, &ze.uCompSize, &ze.uOffset };
				for (uint64_t* pField : arrFields) {
					if (0xFFFFFFFF == *pField && v + 8 <= vEnd) {
						*pField = ZipLE64(v);
						v += 8;
					}
				}
			}
			x = vEnd;
		}
		p = uNext;

		ZUtil::StringReplace(ze.strName, "\\", "/");
		if (ze.strName.empty() || '/' == ze.strName[0] || string::npos != ("/" + ze.strName + "/").find("/../")) {
			ZLog::ErrorV(">>> Unsafe path in archive: %s\n", ze.strName.c_str());
			return -1;
		}
		bUnsupported |= (uFlags & 1);

		bool bFolder = ('/' == ze.strName.back());
		for (size_t pos = ze.strName.find('/'); string::npos != pos; pos = ze.strName.find('/', pos + 1)) {
			setFolders.insert(ze.strName.substr(0, pos));
		}
		if (bFolder) {
			continue;
		}
		bUnsupported |= (0 != ze.uMethod && 8 != ze.uMethod);
		mapLast[ze.strName] = arrEntries.size();
		arrEntries.push_back(ze);
	}
	if (bUnsupported) {
		return 1;
	}

	for (const string& strSub : setFolders) {
		string strPath = strFolder + "/" + strSub;
		if (0 != mkdir(strPath.c_str(), 0755) && EEXIST != errno) {
			return -1;
		}
	}

	vector<size_t> arrOrder;
	uint64_t uTotal = 0;
	for (size_t i = 0; i < arrEntries.size(); i++) {
		if (mapLast[arrEntries[i].strName] == i) {
			arrOrder.push_back(i);
			uTotal += arrEntries[i].uCompSize;
		}
	}
	sort(arrOrder.begin(), arrOrder.end(), [&](size_t a, size_t b) {
		return arrEntries[a].uCompSize > arrEntries[b].uCompSize;
	});

	atomic<bool> bFailed(false);
	mutex mtxProgress;
	uint64_t uDone = 0;
	double dReported = 0;
	ZUtil::ParallelFor(arrOrder.size(), [&](size_t k) {
		const ZZipEntry& ze = arrEntries[arrOrder[k]];
		if (bFailed || !ZipExtractEntry(fd, ze, strFolder + "/" + ze.strName)) {
			if (!bFailed.exchange(true)) {
				ZLog::ErrorV(">>> Failed to extract: %s\n", ze.strName.c_str());
			}
			return;
		}
		if (fnProgress && uTotal > 0) {
			lock_guard<mutex> lock(mtxProgress);
			uDone += ze.uCompSize;
			double dProgress = (double)uDone / uTotal;
			if (dProgress - dReported >= 0.01) {
				dReported = dProgress;
				fnProgress(dProgress);
			}
		}
	});
	return bFailed ? -1 : 0;
}
#endif

bool ZFile::ArchiveZip(const char* szFolder, const char* szZip, const char* szPrefix, const function<void(double)>& fnProgress)
{
#ifdef _WIN32
	return false;
#else
	int out = open(szZip, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (out < 0) {
		return false;
	}
	bool bOK = ZipArchive(out, szFolder, szPrefix, fnProgress);
	bOK = (0 == close(out)) && bOK;
	if (!bOK) {
		unlink(szZip);
	}
	return bOK;
#endif
}

int ZFile::ExtractZip(const char* szZip, const char* szFolder, const function<void(double)>& fnProgress)
{
#ifdef _WIN32
	return 1;
#else
	int fd = open(szZip, O_RDONLY);
	if (fd < 0) {
		return -1;
	}
	int nRet = ZipExtract(fd, szFolder, fnProgress);
	close(fd);
	return nRet;
#endif
}

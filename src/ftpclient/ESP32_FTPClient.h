#include <FS.h>

class ESP32_FTPClient
{
  private:
  void WriteClientBuffered(WiFiClient* cli, unsigned char * data, int dataLength);
  char outBuf[128];
  unsigned char outCount;
  WiFiClient client;
  WiFiClient dclient;
  uint8_t verbose;

  template<typename T>
  		void FTPdbg(T msg) {
    	if(verbose == 2) Serial.print(msg);
		}
  
  template<typename T>
  		void FTPdbgn(T msg) {
    	if(verbose == 2) Serial.println(msg);
		}

    template<typename T>
    void FTPerr(T msg) {
    if(verbose == 1 || verbose == 2) Serial.print(msg);
  }

  char* userName;
  char* passWord;
  char* serverAdress;
  uint16_t port;
  bool _isConnected = false;
  unsigned char clientBuf[1500];
  size_t bufferSize = 1500;
  uint16_t timeout = 10000;
  WiFiClient* GetDataClient();
  
  public:
  ESP32_FTPClient(char* _serverAdress, uint16_t _port, char* _userName, char* _passWord, uint16_t _timeout = 10000, uint8_t _verbose = 1);
  ESP32_FTPClient(char* _serverAdress, char* _userName, char* _passWord, uint16_t _timeout = 10000, uint8_t _verbose = 1);
  void OpenConnection();
  void CloseConnection();
  bool isConnected();
  void NewFile (const char* fileName);
  void AppendFile( char* fileName);
  void WriteData (unsigned char * data, int dataLength);
  void CloseFile ();
  void GetFTPAnswer (char* result = NULL, int offsetStart = 0);
  void GetLastModifiedTime(const char* fileName, char* result);
  void RenameFile(char* from, char* to);
  void Write(const char * str);
  void InitFile(const char* type);
  void ChangeWorkDir(const char * dir);
  void DeleteFile(const char * file);
  void MakeDir(const char * dir);
  void ContentList(const char * dir, String * list);
  void ContentListWithListCommand(const char * dir, String * list);
  void DownloadString(const char * filename, String &str);
  void DownloadFile(const char * filename, unsigned char * buf, size_t length, bool printUART = false);

  // Added locally (not upstream): the original DownloadFile() reads a fixed
  // `length` via readBytes() and discards the actual byte count, so it can't
  // tell real data from trailing garbage on a short read, or truncate safely
  // on a long one - unusable for files whose size isn't known in advance.
  // This instead streams from the passive data socket until it closes
  // (correct FTP semantics for RETR) and returns the real byte count.
  size_t DownloadFileToFile(const char * filename, File &out);
};

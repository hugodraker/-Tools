//gcc -Os -s -mwindows -o mssqldbrestore.exe mssqldbrestore.c -lodbc32 -lcomctl32 -lcomdlg32
#include <windows.h>
#include <commctrl.h>
#include <sql.h>
#include <sqlext.h>
#include <stdio.h>

// UI Element IDs
#define ID_SERVER 101
#define ID_DB 102
#define ID_FILE 103
#define ID_BROWSE 104
#define ID_RESTORE 105
#define ID_LOG 106
#define ID_DISABLE_CONN 107
#define ID_ENABLE_CONN 108
#define ID_DELETE_DB 109

HWND hServer, hDb, hFile, hLog;
char iniPath[MAX_PATH];

// --- Configuration Management ---
void SetupIniPath() {
    GetModuleFileName(NULL, iniPath, MAX_PATH);
    char *p = strrchr(iniPath, '\\');
    if (p) strcpy(p + 1, "mssqldbrestore.ini");
}

void LoadSettings() {
    char buffer[256];
    GetPrivateProfileString("Settings", "Server", ".\\SQLEXPRESS", buffer, sizeof(buffer), iniPath);
    SetWindowText(hServer, buffer);
    
    GetPrivateProfileString("Settings", "Database", "MyDatabase", buffer, sizeof(buffer), iniPath);
    SetWindowText(hDb, buffer);
    
    GetPrivateProfileString("Settings", "LastFile", "", buffer, sizeof(buffer), iniPath);
    SetWindowText(hFile, buffer);
}

void SaveSettings() {
    char buffer[256];
    GetWindowText(hServer, buffer, sizeof(buffer));
    WritePrivateProfileString("Settings", "Server", buffer, iniPath);
    
    GetWindowText(hDb, buffer, sizeof(buffer));
    WritePrivateProfileString("Settings", "Database", buffer, iniPath);
    
    GetWindowText(hFile, buffer, sizeof(buffer));
    WritePrivateProfileString("Settings", "LastFile", buffer, iniPath);
}

// --- Logging and Error Handling ---
void LogMessage(const char* msg) {
    int len = GetWindowTextLength(hLog);
    SendMessage(hLog, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessage(hLog, EM_REPLACESEL, 0, (LPARAM)msg);
    SendMessage(hLog, EM_REPLACESEL, 0, (LPARAM)"\r\n");
}

void LogOdbcError(SQLSMALLINT handleType, SQLHANDLE handle, const char* contextMsg) {
    SQLCHAR sqlState[6], msg[SQL_MAX_MESSAGE_LENGTH];
    SQLINTEGER nativeError;
    SQLSMALLINT msgLen;
    SQLRETURN ret;
    int i = 1;
    char buffer[1024];
    
    sprintf(buffer, "ERROR: %s", contextMsg);
    LogMessage(buffer);

    while ((ret = SQLGetDiagRec(handleType, handle, i, sqlState, &nativeError, 
                                msg, sizeof(msg), &msgLen)) != SQL_NO_DATA) {
        sprintf(buffer, "State: %s, Native Error: %d, Msg: %s", sqlState, (int)nativeError, msg);
        LogMessage(buffer);
        i++;
    }
}

// --- Core Database Logic ---
void RunQuery(HWND hwnd, const char* server, const char* query, const char* successMsg) {
    LogMessage("----------------------------------------");
    LogMessage("Initializing ODBC...");

    SQLHENV hEnv = NULL;
    SQLHDBC hDbc = NULL;
    SQLHSTMT hStmt = NULL;
    SQLRETURN ret;

    SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &hEnv);
    SQLSetEnvAttr(hEnv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
    SQLAllocHandle(SQL_HANDLE_DBC, hEnv, &hDbc);

    const char* drivers[] = {
        "ODBC Driver 17 for SQL Server",
        "ODBC Driver 18 for SQL Server",
        "ODBC Driver 13 for SQL Server",
        "SQL Server Native Client 11.0",
        "SQL Server"
    };
    int numDrivers = sizeof(drivers) / sizeof(drivers[0]);
    BOOL connected = FALSE;

    LogMessage("Connecting to SQL Server (Negotiating Drivers)...");

    for (int i = 0; i < numDrivers; i++) {
        char connStr[512];
        
        if (strstr(drivers[i], "18")) {
            sprintf(connStr, "Driver={%s};Server=%s;Database=master;Trusted_Connection=yes;Encrypt=yes;TrustServerCertificate=yes;", drivers[i], server);
        } else {
            sprintf(connStr, "Driver={%s};Server=%s;Database=master;Trusted_Connection=yes;", drivers[i], server);
        }
        
        SQLCHAR outConnStr[1024];
        SQLSMALLINT outConnStrLen;
        ret = SQLDriverConnect(hDbc, hwnd, (SQLCHAR*)connStr, SQL_NTS, outConnStr, sizeof(outConnStr), &outConnStrLen, SQL_DRIVER_NOPROMPT);

        if (SQL_SUCCEEDED(ret)) {
            connected = TRUE;
            char successLog[256];
            sprintf(successLog, "Connected successfully via: %s", drivers[i]);
            LogMessage(successLog);
            break;
        }
    }

    if (!connected) {
        LogOdbcError(SQL_HANDLE_DBC, hDbc, "All ODBC drivers failed to connect.");
        LogMessage("HINT 1: Ensure your Server name is exactly correct.");
        LogMessage("HINT 2: If using LocalDB, type: (localdb)\\MSSQLLocalDB");
        LogMessage("HINT 3: If default instance, type: .  (just a period)");
    } else {
        LogMessage("Executing command...");
        SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hStmt);

        LogMessage(query);

        ret = SQLExecDirect(hStmt, (SQLCHAR*)query, SQL_NTS);
        
        if (SQL_SUCCEEDED(ret) || ret == SQL_SUCCESS_WITH_INFO) {
            LogMessage(successMsg);
            MessageBox(hwnd, successMsg, "Success", MB_ICONINFORMATION);
        } else {
            LogOdbcError(SQL_HANDLE_STMT, hStmt, "Command failed.");
            LogMessage("HINT: If 'Access Denied', the SQL Server Windows Service account lacks permissions.");
        }

        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
        SQLDisconnect(hDbc);
    }

    SQLFreeHandle(SQL_HANDLE_DBC, hDbc);
    SQLFreeHandle(SQL_HANDLE_ENV, hEnv);
    LogMessage("Disconnected.");
}

void SetDbState(HWND hwnd, BOOL killConnections) {
    char server[256], db[256];
    GetWindowText(hServer, server, sizeof(server));
    GetWindowText(hDb, db, sizeof(db));

    if (!server[0] || !db[0]) {
        MessageBox(hwnd, "Please fill in Server and Database name.", "Missing Info", MB_ICONWARNING);
        return;
    }

    SaveSettings();
    char query[512];

    if (killConnections) {
        sprintf(query, "IF DB_ID('%s') IS NOT NULL ALTER DATABASE [%s] SET SINGLE_USER WITH ROLLBACK IMMEDIATE;", db, db);
        RunQuery(hwnd, server, query, "Connections Killed (Database set to Single User Mode).");
    } else {
        sprintf(query, "IF DB_ID('%s') IS NOT NULL ALTER DATABASE [%s] SET MULTI_USER;", db, db);
        RunQuery(hwnd, server, query, "Connections Enabled (Database set to Multi User Mode).");
    }
}

void ExecuteRestore(HWND hwnd) {
    char server[256], db[256], file[MAX_PATH];
    GetWindowText(hServer, server, sizeof(server));
    GetWindowText(hDb, db, sizeof(db));
    GetWindowText(hFile, file, sizeof(file));

    if (!server[0] || !db[0] || !file[0]) {
        MessageBox(hwnd, "Please fill in Server, Database, and Backup File path.", "Missing Info", MB_ICONWARNING);
        return;
    }

    SaveSettings();

    char query[2048];
    sprintf(query, 
        "IF DB_ID('%s') IS NOT NULL ALTER DATABASE [%s] SET SINGLE_USER WITH ROLLBACK IMMEDIATE; "
        "RESTORE DATABASE [%s] FROM DISK = '%s' WITH REPLACE; "
        "ALTER DATABASE [%s] SET MULTI_USER;", 
        db, db, db, file, db);

    RunQuery(hwnd, server, query, "RESTORE completed successfully!");
}

void DropDatabase(HWND hwnd) {
    char server[256], db[256];
    GetWindowText(hServer, server, sizeof(server));
    GetWindowText(hDb, db, sizeof(db));

    if (!server[0] || !db[0]) {
        MessageBox(hwnd, "Please fill in Server and Target Database Name to drop.", "Missing Info", MB_ICONWARNING);
        return;
    }

    char prompt[512];
    sprintf(prompt, "Are you absolutely sure you want to DROP the database '%s'?\n\nThis action cannot be undone and will permanently delete all data inside it.", db);
    
    // MB_DEFBUTTON2 ensures "No" is highlighted by default to prevent accidental clicks
    if (MessageBox(hwnd, prompt, "Confirm Drop Database", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES) {
        SaveSettings();
        
        char query[1024];
        sprintf(query, 
            "IF DB_ID('%s') IS NOT NULL "
            "BEGIN "
            "ALTER DATABASE [%s] SET SINGLE_USER WITH ROLLBACK IMMEDIATE; "
            "DROP DATABASE [%s]; "
            "END", 
            db, db, db);

        RunQuery(hwnd, server, query, "Database DROPPED successfully.");
    }
}

void BrowseForFile(HWND hwnd) {
    OPENFILENAME ofn;
    char szFile[MAX_PATH] = {0};

    GetWindowText(hFile, szFile, sizeof(szFile));

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "Backup Files (*.dat;*.bak)\0*.dat;*.bak\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;

    if (GetOpenFileName(&ofn)) {
        SetWindowText(hFile, szFile);
    }
}

// Child Window Enumerator for setting font
BOOL CALLBACK SetFontProc(HWND hwndChild, LPARAM lParam) {
    SendMessage(hwndChild, WM_SETFONT, (WPARAM)lParam, TRUE);
    return TRUE;
}

// --- GUI Setup ---
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            CreateWindow("STATIC", "Server (e.g., .\\SQLEXPRESS):", WS_VISIBLE | WS_CHILD, 10, 10, 200, 20, hwnd, NULL, NULL, NULL);
            hServer = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, 10, 30, 250, 25, hwnd, (HMENU)ID_SERVER, NULL, NULL);

            CreateWindow("STATIC", "Target Database Name:", WS_VISIBLE | WS_CHILD, 10, 60, 200, 20, hwnd, NULL, NULL, NULL);
            hDb = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, 10, 80, 250, 25, hwnd, (HMENU)ID_DB, NULL, NULL);

            CreateWindow("STATIC", "Backup File Path (.dat or .bak):", WS_VISIBLE | WS_CHILD, 10, 110, 200, 20, hwnd, NULL, NULL, NULL);
            hFile = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, 10, 130, 480, 25, hwnd, (HMENU)ID_FILE, NULL, NULL);
            CreateWindow("BUTTON", "Browse...", WS_VISIBLE | WS_CHILD, 500, 130, 80, 25, hwnd, (HMENU)ID_BROWSE, NULL, NULL);

            // Three utility buttons split evenly across the width
            CreateWindow("BUTTON", "Kill Connections", WS_VISIBLE | WS_CHILD, 10, 170, 180, 25, hwnd, (HMENU)ID_DISABLE_CONN, NULL, NULL);
            CreateWindow("BUTTON", "Enable Connections", WS_VISIBLE | WS_CHILD, 200, 170, 180, 25, hwnd, (HMENU)ID_ENABLE_CONN, NULL, NULL);
            CreateWindow("BUTTON", "Drop Database", WS_VISIBLE | WS_CHILD, 390, 170, 190, 25, hwnd, (HMENU)ID_DELETE_DB, NULL, NULL);

            // Main Restore action
            CreateWindow("BUTTON", "RESTORE DATABASE", WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 10, 205, 570, 35, hwnd, (HMENU)ID_RESTORE, NULL, NULL);

            CreateWindow("STATIC", "Status / Log:", WS_VISIBLE | WS_CHILD, 10, 250, 200, 20, hwnd, NULL, NULL, NULL);
            hLog = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "", WS_VISIBLE | WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, 10, 270, 570, 170, hwnd, (HMENU)ID_LOG, NULL, NULL);
            
            HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            SendMessage(hwnd, WM_SETFONT, (WPARAM)hFont, TRUE);
            EnumChildWindows(hwnd, SetFontProc, (LPARAM)hFont);

            SetupIniPath();
            LoadSettings();
            break;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == ID_BROWSE) BrowseForFile(hwnd);
            if (LOWORD(wParam) == ID_RESTORE) ExecuteRestore(hwnd);
            if (LOWORD(wParam) == ID_DISABLE_CONN) SetDbState(hwnd, TRUE);
            if (LOWORD(wParam) == ID_ENABLE_CONN) SetDbState(hwnd, FALSE);
            if (LOWORD(wParam) == ID_DELETE_DB) DropDatabase(hwnd);
            break;
        case WM_DESTROY:
            SaveSettings();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    InitCommonControls();
    
    WNDCLASS wc = {0};
    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "SqlRestoreToolClass";

    RegisterClass(&wc);

    HWND hwnd = CreateWindow("SqlRestoreToolClass", "Win32 SQL Restore GUI", 
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 610, 490,
        NULL, NULL, hInstance, NULL);

    ShowWindow(hwnd, nCmdShow);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
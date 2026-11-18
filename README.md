# 🛠️ -Tools

A collection of lightweight, standalone C utilities, system monitors, developers tools, and desktop applications curated by **Hugo**. Most tools come with pre-compiled Windows executables (`.exe`) alongside their source code (`.c`).

## 🗂️ Tool Directory

Here is an overview of the utilities included in this repository, categorized by their primary function:

### 🖼️ Imaging & Graphics
* **`IconVectorizer.c` / `iconeditv.c` / `icons.c`**: Utilities for processing, editing, and vectorizing icons or image assets.
* **`shell-icons.c`**: Shell icon extraction and management helper.

### 🏥 Medical & DICOM
* **`DicomViewer.c`**: A lightweight DICOM medical image viewer.
* **`dicom_editor.c`** (and `.java`): Tool for editing DICOM files and metadata.

### 📊 Data & Documents
* **`csvsql.c` / `csvsqlc.c`**: Query or process CSV files using SQL-like syntax.
* **`xlsx_edit.c`**: Lightweight tool for editing Excel (`.xlsx`) spreadsheets.
* **`receipts_pdf.c`**: Generate PDF receipts programmatically.

### 📦 Barcodes & QR
* **`barcodegen.c` / `BarcodeGen.exe`**: Generate various barcode formats.
* **`barcodescan.c` / `BarcodeScanDM.c` / `BarcodeScanQR.c`**: Scan and decode 1D, Data Matrix, and QR barcodes (assisted by `pdf417_clusters.h` and `stb_image.h`).

### ⚙️ System & Administration
* **`ProcManager.c`**: GUI .c file function editor.
* **`batmon.c`**: Battery monitor utility.
* **`usbmon.c`**: USB device monitoring tool.
* **`wificon.c`**: Wi-Fi connection utility.
* **`taskbar.c`**: Custom taskbar utility or overlay.
* **`osk.c`**: On-screen keyboard utility.
* **`mssqldbrestore.c`**: Utility for Microsoft SQL Server database restoration.
* **`vhdmaster.c`**: Virtual Hard Disk (VHD) management tool.
* **`isomaster.c`**: ISO image manipulation tool.
* **`wkspacer.c`**: Workspace/virtual desktop spacer utility.
* **`e1plorer.c`**: Custom file explorer or alternative shell utility.

### 🤖 AI & Development Utilities
* **`ai01.c` / `ai02.c`**: Experimental AI or machine learning utility scripts.
* **`cnccutter.c`**: Tool related to CNC cutting paths or G-code utility.
* **`cbak_standalone.c`**: Standalone backup utility.
* **`gui_compiler.c`**: Helper utility for compiling or managing GUI components.
* **`shellchanger.hta`**: HTML Application script for shell customization.

---

## 🚀 Getting Started

Clone the repository to inspect the source code or run the included binaries:

```bash
git clone https://github.com/hugodraker/-Tools.git
cd -Tools
```

## 📋 Compilation

Most `.c` files are designed to be standalone programs. You can compile them using standard C compilers like GCC:

```bash
gcc ProcManager.c -o ProcManager.exe
```

## 🤝 Contributing

Contributions, suggestions, and improvements are always welcome! Feel free to open a Pull Request or submit an issue.

## 📝 License

Distributed under the Unlicense. Released into the public domain

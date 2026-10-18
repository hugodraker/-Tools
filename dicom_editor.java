import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class DicomEditor {

    static class EditRule implements Comparable<EditRule> {
        int group;
        int elem;
        byte[] newVal;
        boolean applied;

        @Override
        public int compareTo(EditRule o) {
            if (this.group != o.group) return Integer.compare(this.group, o.group);
            return Integer.compare(this.elem, o.elem);
        }
    }

    static class TagDict {
        String name;
        int group;
        int elem;
        String vr;

        TagDict(String name, int group, int elem, String vr) {
            this.name = name;
            this.group = group;
            this.elem = elem;
            this.vr = vr;
        }
    }

    private static final TagDict[] DICT = {
        // File Meta Information
        new TagDict("FileMetaInformationVersion", 0x0002, 0x0001, "OB"),
        new TagDict("MediaStorageSOPClassUID", 0x0002, 0x0002, "UI"),
        new TagDict("MediaStorageSOPInstanceUID", 0x0002, 0x0003, "UI"),
        new TagDict("TransferSyntaxUID", 0x0002, 0x0010, "UI"),
        new TagDict("ImplementationClassUID", 0x0002, 0x0012, "UI"),
        new TagDict("ImplementationVersionName", 0x0002, 0x0013, "SH"),
        new TagDict("SourceApplicationEntityTitle", 0x0002, 0x0016, "AE"),

        // Identifying Information Group
        new TagDict("ImageType", 0x0008, 0x0008, "CS"),
        new TagDict("InstanceCreationDate", 0x0008, 0x0012, "DA"),
        new TagDict("InstanceCreationTime", 0x0008, 0x0013, "TM"),
        new TagDict("SOPClassUID", 0x0008, 0x0016, "UI"),
        new TagDict("SOPInstanceUID", 0x0008, 0x0018, "UI"),
        new TagDict("StudyDate", 0x0008, 0x0020, "DA"),
        new TagDict("SeriesDate", 0x0008, 0x0021, "DA"),
        new TagDict("AcquisitionDate", 0x0008, 0x0022, "DA"),
        new TagDict("ContentDate", 0x0008, 0x0023, "DA"),
        new TagDict("StudyTime", 0x0008, 0x0030, "TM"),
        new TagDict("SeriesTime", 0x0008, 0x0031, "TM"),
        new TagDict("AcquisitionTime", 0x0008, 0x0032, "TM"),
        new TagDict("ContentTime", 0x0008, 0x0033, "TM"),
        new TagDict("AccessionNumber", 0x0008, 0x0050, "SH"),
        new TagDict("Modality", 0x0008, 0x0060, "CS"),
        new TagDict("Manufacturer", 0x0008, 0x0070, "LO"),
        new TagDict("InstitutionName", 0x0008, 0x0080, "LO"),
        new TagDict("InstitutionAddress", 0x0008, 0x0081, "ST"),
        new TagDict("ReferringPhysicianName", 0x0008, 0x0090, "PN"),
        new TagDict("StationName", 0x0008, 0x1010, "SH"),
        new TagDict("StudyDescription", 0x0008, 0x1030, "LO"),
        new TagDict("SeriesDescription", 0x0008, 0x103E, "LO"),
        new TagDict("InstitutionalDepartmentName", 0x0008, 0x1040, "LO"),
        new TagDict("PhysiciansOfRecord", 0x0008, 0x1048, "PN"),
        new TagDict("PerformingPhysicianName", 0x0008, 0x1050, "PN"),
        new TagDict("NameOfPhysiciansReadingStudy", 0x0008, 0x1060, "PN"),
        new TagDict("OperatorsName", 0x0008, 0x1070, "PN"),
        new TagDict("ManufacturerModelName", 0x0008, 0x1090, "LO"),

        // Patient Information Group
        new TagDict("PatientName", 0x0010, 0x0010, "PN"),
        new TagDict("PatientID", 0x0010, 0x0020, "LO"),
        new TagDict("IssuerOfPatientID", 0x0010, 0x0021, "LO"),
        new TagDict("PatientBirthDate", 0x0010, 0x0030, "DA"),
        new TagDict("PatientBirthTime", 0x0010, 0x0032, "TM"),
        new TagDict("PatientSex", 0x0010, 0x0040, "CS"),
        new TagDict("OtherPatientIDs", 0x0010, 0x1000, "LO"),
        new TagDict("OtherPatientNames", 0x0010, 0x1001, "PN"),
        new TagDict("PatientAge", 0x0010, 0x1010, "AS"),
        new TagDict("PatientSize", 0x0010, 0x1020, "DS"),
        new TagDict("PatientWeight", 0x0010, 0x1030, "DS"),
        new TagDict("MedicalAlerts", 0x0010, 0x2000, "LO"),
        new TagDict("Allergies", 0x0010, 0x2110, "LO"),
        new TagDict("PregnancyStatus", 0x0010, 0x21C0, "US"),

        // Relationship Group
        new TagDict("StudyInstanceUID", 0x0020, 0x000D, "UI"),
        new TagDict("SeriesInstanceUID", 0x0020, 0x000E, "UI"),
        new TagDict("StudyID", 0x0020, 0x0010, "SH"),
        new TagDict("SeriesNumber", 0x0020, 0x0011, "IS"),
        new TagDict("AcquisitionNumber", 0x0020, 0x0012, "IS"),
        new TagDict("InstanceNumber", 0x0020, 0x0013, "IS"),

        // Image Group
        new TagDict("SamplesPerPixel", 0x0028, 0x0002, "US"),
        new TagDict("Rows", 0x0028, 0x0010, "US"),
        new TagDict("Columns", 0x0028, 0x0011, "US"),
        
        // Pixel Data
        new TagDict("PixelData", 0x7FE0, 0x0010, "OW")
    };

    private static String getVrForTag(int group, int elem) {
        for (TagDict dict : DICT) {
            if (dict.group == group && dict.elem == elem) return dict.vr;
        }
        return "LO"; // Default explicit VR for unknown injected tags
    }

    private static byte[] unescapeString(String str) {
        ByteArrayOutputStream bos = new ByteArrayOutputStream();
        int i = 0;
        while (i < str.length()) {
            char c = str.charAt(i);
            if (c == '\\' && i + 1 < str.length()) {
                i++;
                char next = str.charAt(i);
                switch (next) {
                    case 'n': bos.write('\n'); break;
                    case 't': bos.write('\t'); break;
                    case 'r': bos.write('\r'); break;
                    case '\\': bos.write('\\'); break;
                    case '"': bos.write('"'); break;
                    case 'x':
                        if (i + 2 < str.length()) {
                            try {
                                int hexVal = Integer.parseInt(str.substring(i + 1, i + 3), 16);
                                bos.write(hexVal);
                                i += 2;
                            } catch (NumberFormatException e) {
                                bos.write('x');
                            }
                        } else {
                            bos.write('x');
                        }
                        break;
                    default: bos.write(next); break;
                }
            } else {
                bos.write(c);
            }
            i++;
        }
        return bos.toByteArray();
    }

    private static int[] parseTag(String tagStr) {
        String clean = tagStr.replaceAll("\\s+", "");

        for (TagDict d : DICT) {
            if (d.name.equalsIgnoreCase(clean)) {
                return new int[]{d.group, d.elem};
            }
        }

        Pattern[] patterns = {
            Pattern.compile("\\(?(?:0x)?([0-9a-fA-F]+),(?:0x)?([0-9a-fA-F]+)\\)?"),
            Pattern.compile("([0-9a-fA-F]{4})([0-9a-fA-F]{4})")
        };

        for (Pattern p : patterns) {
            Matcher m = p.matcher(clean);
            if (m.matches()) {
                return new int[]{Integer.parseInt(m.group(1), 16), Integer.parseInt(m.group(2), 16)};
            }
        }
        return null;
    }

    private static void injectTag(OutputStream fout, EditRule rule, boolean isExplicit) throws IOException {
        writeLittleEndianShort(fout, rule.group);
        writeLittleEndianShort(fout, rule.elem);

        int newLen = rule.newVal.length;
        boolean needsPad = (newLen % 2 != 0);
        if (needsPad) newLen++;

        String vr = getVrForTag(rule.group, rule.elem);

        if (isExplicit) {
            fout.write(vr.getBytes(StandardCharsets.US_ASCII));
            if (vr.equals("OB") || vr.equals("OW") || vr.equals("OF") || vr.equals("SQ") ||
                vr.equals("UT") || vr.equals("UN") || vr.equals("UR") || vr.equals("UC")) {
                writeLittleEndianShort(fout, 0); // reserved
                writeLittleEndianInt(fout, newLen);
            } else {
                writeLittleEndianShort(fout, newLen);
            }
        } else {
            writeLittleEndianInt(fout, newLen);
        }

        fout.write(rule.newVal);
        if (needsPad) {
            fout.write(vr.equals("UI") ? 0x00 : 0x20); // \0 for UI, Space for others
        }
    }

    // --- Little Endian I/O Helpers ---
    private static int readLittleEndianShort(InputStream in) throws IOException {
        int ch1 = in.read();
        int ch2 = in.read();
        if ((ch1 | ch2) < 0) return -1;
        return (ch2 << 8) + (ch1 << 0);
    }

    private static long readLittleEndianInt(InputStream in) throws IOException {
        int ch1 = in.read();
        int ch2 = in.read();
        int ch3 = in.read();
        int ch4 = in.read();
        if ((ch1 | ch2 | ch3 | ch4) < 0) throw new EOFException();
        return ((long) (ch4 << 24) + (ch3 << 16) + (ch2 << 8) + (ch1 << 0)) & 0xFFFFFFFFL;
    }

    private static void writeLittleEndianShort(OutputStream out, int v) throws IOException {
        out.write((v >>> 0) & 0xFF);
        out.write((v >>> 8) & 0xFF);
    }

    private static void writeLittleEndianInt(OutputStream out, long v) throws IOException {
        out.write((int) ((v >>> 0) & 0xFF));
        out.write((int) ((v >>> 8) & 0xFF));
        out.write((int) ((v >>> 16) & 0xFF));
        out.write((int) ((v >>> 24) & 0xFF));
    }

    public static void main(String[] args) {
        String delim = new String(new byte[]{(byte) 0xE2, (byte) 0x80, (byte) 0xA2}, StandardCharsets.UTF_8);
        String inputFile = null;
        String outputFile = null;
        String rulesStr = null;
        boolean overwrite = false;

        List<String> posArgs = new ArrayList<>();

        for (String arg : args) {
            if (arg.startsWith("-d:")) {
                delim = new String(unescapeString(arg.substring(3)), StandardCharsets.UTF_8);
            } else {
                if (posArgs.size() < 3) posArgs.add(arg);
            }
        }

        if (posArgs.size() == 2) {
            inputFile = posArgs.get(0);
            outputFile = "dicom_temp_out.dcm";
            rulesStr = posArgs.get(1);
            overwrite = true;
        } else if (posArgs.size() == 3) {
            inputFile = posArgs.get(0);
            outputFile = posArgs.get(1);
            rulesStr = posArgs.get(2);
        } else {
            System.out.println("Usage: java DicomEditor [-d:DELIM] <input.dcm> [output.dcm] \"RULES\"");
            return;
        }

        List<EditRule> rules = new ArrayList<>();
        String[] ruleTokens = rulesStr.split(Pattern.quote(delim));
        for (String token : ruleTokens) {
            int eqIdx = token.indexOf('=');
            if (eqIdx != -1) {
                String tagStr = token.substring(0, eqIdx);
                String valStr = token.substring(eqIdx + 1);
                int[] parsed = parseTag(tagStr);
                if (parsed != null) {
                    EditRule rule = new EditRule();
                    rule.group = parsed[0];
                    rule.elem = parsed[1];
                    rule.newVal = unescapeString(valStr);
                    rule.applied = false;
                    rules.add(rule);
                } else {
                    System.out.println("Warning: Could not parse tag '" + tagStr + "'");
                }
            }
        }
        Collections.sort(rules);

        File inF = new File(inputFile);
        File outF = new File(outputFile);

        try (BufferedInputStream fin = new BufferedInputStream(new FileInputStream(inF));
             BufferedOutputStream fout = new BufferedOutputStream(new FileOutputStream(outF))) {

            byte[] header = new byte[132];
            int bytesRead = fin.read(header);
            if (bytesRead != 132 || !new String(header, 128, 4, StandardCharsets.US_ASCII).equals("DICM")) {
                System.out.println("Not a valid DICOM file.");
                return;
            }
            fout.write(header);

            boolean inMeta = true;
            boolean isExplicit = true;
            boolean tsExplicit = true;

            int group;
            while ((group = readLittleEndianShort(fin)) != -1) {
                int elem = readLittleEndianShort(fin);

                if (inMeta && group > 0x0002) {
                    inMeta = false;
                    isExplicit = tsExplicit;
                }

                boolean isSqItem = (group == 0xFFFE);

                if (!isSqItem) {
                    for (EditRule r : rules) {
                        if (!r.applied && (r.group < group || (r.group == group && r.elem < elem))) {
                            injectTag(fout, r, r.group <= 0x0002 ? tsExplicit : isExplicit);
                            r.applied = true;
                        }
                    }
                }

                String vrStr = "";
                long length = 0;
                boolean is32bitLen = false;

                if (isExplicit && !isSqItem) {
                    byte[] vrBytes = new byte[2];
                    fin.read(vrBytes);
                    vrStr = new String(vrBytes, StandardCharsets.US_ASCII);

                    if (vrStr.equals("OB") || vrStr.equals("OW") || vrStr.equals("OF") || vrStr.equals("SQ") ||
                        vrStr.equals("UT") || vrStr.equals("UN") || vrStr.equals("UR") || vrStr.equals("UC")) {
                        readLittleEndianShort(fin); // reserved
                        length = readLittleEndianInt(fin);
                        is32bitLen = true;
                    } else {
                        length = readLittleEndianShort(fin);
                    }
                } else {
                    length = readLittleEndianInt(fin);
                    is32bitLen = true;
                }

                EditRule matchedRule = null;
                for (EditRule r : rules) {
                    if (r.group == group && r.elem == elem) {
                        matchedRule = r; break;
                    }
                }

                writeLittleEndianShort(fout, group);
                writeLittleEndianShort(fout, elem);

                if (matchedRule != null && !isSqItem) {
                    matchedRule.applied = true;
                    int newLen = matchedRule.newVal.length;
                    boolean needsPad = (newLen % 2 != 0);
                    if (needsPad) newLen++;

                    if (isExplicit) {
                        fout.write(vrStr.getBytes(StandardCharsets.US_ASCII));
                        if (is32bitLen) {
                            writeLittleEndianShort(fout, 0);
                            writeLittleEndianInt(fout, newLen);
                        } else {
                            writeLittleEndianShort(fout, newLen);
                        }
                    } else {
                        writeLittleEndianInt(fout, newLen);
                    }

                    fout.write(matchedRule.newVal);
                    if (needsPad) fout.write(vrStr.equals("UI") ? 0x00 : 0x20);

                    if (length > 0 && length != 0xFFFFFFFFL) fin.skip(length);

                } else {
                    if (isExplicit && !isSqItem) {
                        fout.write(vrStr.getBytes(StandardCharsets.US_ASCII));
                        if (is32bitLen) {
                            writeLittleEndianShort(fout, 0);
                            writeLittleEndianInt(fout, length);
                        } else {
                            writeLittleEndianShort(fout, (int) length);
                        }
                    } else {
                        writeLittleEndianInt(fout, length);
                    }

                    if (group == 0x0002 && elem == 0x0010 && length > 0) {
                        byte[] tsBytes = new byte[(int) length];
                        fin.read(tsBytes);
                        fout.write(tsBytes);
                        String ts = new String(tsBytes, StandardCharsets.US_ASCII);

                        if (ts.startsWith("1.2.840.10008.1.2.1")) tsExplicit = true;
                        else if (ts.startsWith("1.2.840.10008.1.2")) tsExplicit = false;
                    } else if (length > 0 && length != 0xFFFFFFFFL) {
                        byte[] buffer = new byte[8192];
                        long remaining = length;
                        while (remaining > 0) {
                            int toRead = (int) Math.min(buffer.length, remaining);
                            int read = fin.read(buffer, 0, toRead);
                            if (read == -1) break;
                            fout.write(buffer, 0, read);
                            remaining -= read;
                        }
                    }
                }
            }

            for (EditRule r : rules) {
                if (!r.applied) {
                    injectTag(fout, r, r.group <= 0x0002 ? tsExplicit : isExplicit);
                    r.applied = true;
                }
            }

        } catch (IOException e) {
            e.printStackTrace();
        }

        if (overwrite) {
            if (!inF.delete() || !outF.renameTo(inF)) {
                System.out.println("Error overwriting original file. Output saved to " + outputFile);
            } else {
                System.out.println("Original DICOM overwritten successfully.");
            }
        } else {
            System.out.println("DICOM modified successfully.");
        }
    }
}
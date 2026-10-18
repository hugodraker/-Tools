/*
 * ============================================================================
 * 
 *  DICOM Header Editor Implementation - PUBLIC DOMAIN
 *  
 *  LICENSE: This code is released into the PUBLIC DOMAIN worldwide.
 *           Where public domain dedication is not legally recognized,
 *           you may treat this as MIT/0/BSD-0 equivalent.
 *  
 *  DISCLAIMER: This software is provided "AS IS", WITHOUT ANY WARRANTY,
 *              EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO WARRANTIES
 *              OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND
 *              NON-INFRINGEMENT. THIS CODE IS NOT FIT FOR ANY PURPOSE.
 *              DO NOT USE IN PRODUCTION ENVIRONMENTS. NO AUTHENTICATION,
 *              ENCRYPTION, OR SECURITY MECHANISMS ARE IMPLEMENTED.
 *  
 *  COMPILATION INSTRUCTIONS (MinGW-w64 on Windows):
 *      gcc dicom_editor.c -o "dicom_editor.exe" -O2 -Wall
 *      
 *  COMPILATION INSTRUCTIONS (Cross-compile from Linux):
 *      x86_64-w64-mingw32-gcc -static -o dicom_editor.exe dicom_editor.c
 *      
 *  COMPILATION INSTRUCTIONS (32-bit Windows from Linux):
 *      i686-w64-mingw32-gcc -static -o dicom_editor.exe dicom_editor.c
 *      
 *  RUNTIME:
 *      ./dicom_editor.exe [-d:DELIMITER] <input.dcm> [output.dcm] "RULES"
 *      
 *  ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define MAX_RULES 256

typedef struct {
    uint16_t group;
    uint16_t elem;
    char *new_val;
    int applied;
} EditRule;

typedef struct {
    const char *name;
    uint16_t group;
    uint16_t elem;
    const char *vr;
} TagDict;

/* 
 * Comprehensive DICOM Data Dictionary
 * Includes all common Meta, Identifying, Patient, Acquisition, and Image tags.
 * Any unlisted tag can still be matched using hex tuple or continuous hex notation.
 */
TagDict DICT[] = {
    // File Meta Information
    {"FileMetaInformationVersion", 0x0002, 0x0001, "OB"},
    {"MediaStorageSOPClassUID", 0x0002, 0x0002, "UI"},
    {"MediaStorageSOPInstanceUID", 0x0002, 0x0003, "UI"},
    {"TransferSyntaxUID", 0x0002, 0x0010, "UI"},
    {"ImplementationClassUID", 0x0002, 0x0012, "UI"},
    {"ImplementationVersionName", 0x0002, 0x0013, "SH"},
    {"SourceApplicationEntityTitle", 0x0002, 0x0016, "AE"},

    // Identifying Information Group
    {"ImageType", 0x0008, 0x0008, "CS"},
    {"InstanceCreationDate", 0x0008, 0x0012, "DA"},
    {"InstanceCreationTime", 0x0008, 0x0013, "TM"},
    {"SOPClassUID", 0x0008, 0x0016, "UI"},
    {"SOPInstanceUID", 0x0008, 0x0018, "UI"},
    {"StudyDate", 0x0008, 0x0020, "DA"},
    {"SeriesDate", 0x0008, 0x0021, "DA"},
    {"AcquisitionDate", 0x0008, 0x0022, "DA"},
    {"ContentDate", 0x0008, 0x0023, "DA"},
    {"StudyTime", 0x0008, 0x0030, "TM"},
    {"SeriesTime", 0x0008, 0x0031, "TM"},
    {"AcquisitionTime", 0x0008, 0x0032, "TM"},
    {"ContentTime", 0x0008, 0x0033, "TM"},
    {"AccessionNumber", 0x0008, 0x0050, "SH"},
    {"Modality", 0x0008, 0x0060, "CS"},
    {"Manufacturer", 0x0008, 0x0070, "LO"},
    {"InstitutionName", 0x0008, 0x0080, "LO"},
    {"InstitutionAddress", 0x0008, 0x0081, "ST"},
    {"ReferringPhysicianName", 0x0008, 0x0090, "PN"},
    {"StationName", 0x0008, 0x1010, "SH"},
    {"StudyDescription", 0x0008, 0x1030, "LO"},
    {"SeriesDescription", 0x0008, 0x103E, "LO"},
    {"InstitutionalDepartmentName", 0x0008, 0x1040, "LO"},
    {"PhysiciansOfRecord", 0x0008, 0x1048, "PN"},
    {"PerformingPhysicianName", 0x0008, 0x1050, "PN"},
    {"NameOfPhysiciansReadingStudy", 0x0008, 0x1060, "PN"},
    {"OperatorsName", 0x0008, 0x1070, "PN"},
    {"ManufacturerModelName", 0x0008, 0x1090, "LO"},

    // Patient Information Group
    {"PatientName", 0x0010, 0x0010, "PN"},
    {"PatientID", 0x0010, 0x0020, "LO"},
    {"IssuerOfPatientID", 0x0010, 0x0021, "LO"},
    {"PatientBirthDate", 0x0010, 0x0030, "DA"},
    {"PatientBirthTime", 0x0010, 0x0032, "TM"},
    {"PatientSex", 0x0010, 0x0040, "CS"},
    {"OtherPatientIDs", 0x0010, 0x1000, "LO"},
    {"OtherPatientNames", 0x0010, 0x1001, "PN"},
    {"PatientAge", 0x0010, 0x1010, "AS"},
    {"PatientSize", 0x0010, 0x1020, "DS"},
    {"PatientWeight", 0x0010, 0x1030, "DS"},
    {"MedicalAlerts", 0x0010, 0x2000, "LO"},
    {"Allergies", 0x0010, 0x2110, "LO"},
    {"PregnancyStatus", 0x0010, 0x21C0, "US"},

    // Clinical Trial Group
    {"ClinicalTrialSponsorName", 0x0012, 0x0010, "LO"},
    {"ClinicalTrialProtocolID", 0x0012, 0x0020, "LO"},
    {"ClinicalTrialSiteID", 0x0012, 0x0030, "LO"},
    {"ClinicalTrialSiteName", 0x0012, 0x0040, "LO"},
    {"ClinicalTrialSubjectID", 0x0012, 0x0050, "LO"},

    // Acquisition Group
    {"ContrastBolusAgent", 0x0018, 0x0010, "LO"},
    {"ScanningSequence", 0x0018, 0x0020, "CS"},
    {"SequenceVariant", 0x0018, 0x0021, "CS"},
    {"ScanOptions", 0x0018, 0x0022, "CS"},
    {"MRAcquisitionType", 0x0018, 0x0023, "CS"},
    {"SliceThickness", 0x0018, 0x0050, "DS"},
    {"KVP", 0x0018, 0x0060, "DS"},
    {"RepetitionTime", 0x0018, 0x0080, "DS"},
    {"EchoTime", 0x0018, 0x0081, "DS"},
    {"InversionTime", 0x0018, 0x0082, "DS"},
    {"NumberOfAverages", 0x0018, 0x0083, "DS"},
    {"ImagingFrequency", 0x0018, 0x0084, "DS"},
    {"ImagedNucleus", 0x0018, 0x0085, "SH"},
    {"EchoNumbers", 0x0018, 0x0086, "IS"},
    {"MagneticFieldStrength", 0x0018, 0x0087, "DS"},
    {"SpacingBetweenSlices", 0x0018, 0x0088, "DS"},
    {"NumberOfPhaseEncodingSteps", 0x0018, 0x0089, "IS"},
    {"DataCollectionDiameter", 0x0018, 0x0090, "DS"},
    {"EchoTrainLength", 0x0018, 0x0091, "IS"},
    {"PercentSampling", 0x0018, 0x0093, "DS"},
    {"PercentPhaseFieldOfView", 0x0018, 0x0094, "DS"},
    {"PixelBandwidth", 0x0018, 0x0095, "DS"},
    {"DeviceSerialNumber", 0x0018, 0x1000, "LO"},
    {"SoftwareVersions", 0x0018, 0x1020, "LO"},
    {"ProtocolName", 0x0018, 0x1030, "LO"},
    {"DistanceSourceToDetector", 0x0018, 0x1110, "DS"},
    {"DistanceSourceToPatient", 0x0018, 0x1111, "DS"},
    {"GantryDetectorTilt", 0x0018, 0x1120, "DS"},
    {"TableHeight", 0x0018, 0x1130, "DS"},
    {"RotationDirection", 0x0018, 0x1140, "CS"},
    {"ExposureTime", 0x0018, 0x1150, "IS"},
    {"XRayTubeCurrent", 0x0018, 0x1151, "IS"},
    {"Exposure", 0x0018, 0x1152, "IS"},
    {"FilterType", 0x0018, 0x1160, "SH"},
    {"GeneratorPower", 0x0018, 0x1170, "IS"},
    {"FocalSpots", 0x0018, 0x1190, "DS"},
    {"ConvolutionKernel", 0x0018, 0x1210, "SH"},

    // Relationship Group
    {"StudyInstanceUID", 0x0020, 0x000D, "UI"},
    {"SeriesInstanceUID", 0x0020, 0x000E, "UI"},
    {"StudyID", 0x0020, 0x0010, "SH"},
    {"SeriesNumber", 0x0020, 0x0011, "IS"},
    {"AcquisitionNumber", 0x0020, 0x0012, "IS"},
    {"InstanceNumber", 0x0020, 0x0013, "IS"},
    {"PatientOrientation", 0x0020, 0x0020, "CS"},
    {"ImagePositionPatient", 0x0020, 0x0032, "DS"},
    {"ImageOrientationPatient", 0x0020, 0x0037, "DS"},
    {"FrameOfReferenceUID", 0x0020, 0x0052, "UI"},
    {"PositionReferenceIndicator", 0x0020, 0x1040, "LO"},
    {"SliceLocation", 0x0020, 0x1041, "DS"},

    // Image Group
    {"SamplesPerPixel", 0x0028, 0x0002, "US"},
    {"PhotometricInterpretation", 0x0028, 0x0004, "CS"},
    {"PlanarConfiguration", 0x0028, 0x0006, "US"},
    {"NumberOfFrames", 0x0028, 0x0008, "IS"},
    {"Rows", 0x0028, 0x0010, "US"},
    {"Columns", 0x0028, 0x0011, "US"},
    {"PixelSpacing", 0x0028, 0x0030, "DS"},
    {"BitsAllocated", 0x0028, 0x0100, "US"},
    {"BitsStored", 0x0028, 0x0101, "US"},
    {"HighBit", 0x0028, 0x0102, "US"},
    {"PixelRepresentation", 0x0028, 0x0103, "US"},
    {"SmallestImagePixelValue", 0x0028, 0x0106, "US"},
    {"LargestImagePixelValue", 0x0028, 0x0107, "US"},
    {"WindowCenter", 0x0028, 0x1050, "DS"},
    {"WindowWidth", 0x0028, 0x1051, "DS"},
    {"RescaleIntercept", 0x0028, 0x1052, "DS"},
    {"RescaleSlope", 0x0028, 0x1053, "DS"},
    {"RescaleType", 0x0028, 0x1054, "LO"},
    {"LossyImageCompression", 0x0028, 0x2110, "CS"},
    {"LossyImageCompressionRatio", 0x0028, 0x2112, "DS"},

    // Study Component
    {"RequestAttributesSequence", 0x0040, 0x0275, "SQ"},
    {"RequestedProcedureID", 0x0040, 0x1001, "SH"},
    {"RequestedProcedureDescription", 0x0040, 0x1005, "LO"},
    
    // Pixel Data
    {"PixelData", 0x7FE0, 0x0010, "OW"},
    
    {NULL, 0, 0, NULL}
};

const char* get_vr_for_tag(uint16_t group, uint16_t elem) {
    for (int i = 0; DICT[i].name != NULL; i++) {
        if (DICT[i].group == group && DICT[i].elem == elem) {
            return DICT[i].vr;
        }
    }
    return "LO"; // Default explicit VR for unknown injected tags
}

void inject_tag(FILE *fout, EditRule *rule, int is_explicit) {
    fwrite(&rule->group, 2, 1, fout);
    fwrite(&rule->elem, 2, 1, fout);
    
    uint32_t new_len = (uint32_t)strlen(rule->new_val);
    int needs_pad = (new_len % 2 != 0);
    if (needs_pad) new_len++;

    const char *vr = get_vr_for_tag(rule->group, rule->elem);
    
    if (is_explicit) {
        fwrite(vr, 1, 2, fout);
        if (memcmp(vr, "OB", 2)==0 || memcmp(vr, "OW", 2)==0 || memcmp(vr, "OF", 2)==0 ||
            memcmp(vr, "SQ", 2)==0 || memcmp(vr, "UT", 2)==0 || memcmp(vr, "UN", 2)==0 ||
            memcmp(vr, "UR", 2)==0 || memcmp(vr, "UC", 2)==0) {
            uint16_t res = 0;
            fwrite(&res, 2, 1, fout);
            fwrite(&new_len, 4, 1, fout);
        } else {
            uint16_t l16 = (uint16_t)new_len;
            fwrite(&l16, 2, 1, fout);
        }
    } else {
        fwrite(&new_len, 4, 1, fout);
    }
    
    fwrite(rule->new_val, 1, strlen(rule->new_val), fout);
    if (needs_pad) {
        char pad = (memcmp(vr, "UI", 2) == 0) ? '\0' : ' '; 
        fwrite(&pad, 1, 1, fout);
    }
}

// C-String unescaping logic for both values and delimiters
void unescape_string(char *str) {
    char *read = str, *write = str;
    while (*read) {
        if (*read == '\\' && *(read+1)) {
            read++;
            switch(*read) {
                case 'n': *write++ = '\n'; break;
                case 't': *write++ = '\t'; break;
                case 'r': *write++ = '\r'; break;
                case '\\': *write++ = '\\'; break;
                case '"': *write++ = '"'; break;
                case 'x': 
                    if (*(read+1) && *(read+2)) {
                        char hex[3] = {*(read+1), *(read+2), '\0'};
                        *write++ = (char)strtol(hex, NULL, 16);
                        read += 2;
                    } else {
                        *write++ = 'x';
                    }
                    break;
                default: *write++ = *read; break;
            }
        } else {
            *write++ = *read;
        }
        read++;
    }
    *write = '\0';
}

// Parses the tag definition into Group and Element (Supports dictionary & raw numerical notation)
int parse_tag(const char *tag_str, uint16_t *group, uint16_t *elem) {
    unsigned int g = 0, e = 0;
    char clean[128];
    int j = 0;
    
    for (int i = 0; tag_str[i] && j < 127; i++) {
        if (tag_str[i] != ' ' && tag_str[i] != '\t') {
            clean[j++] = tag_str[i];
        }
    }
    clean[j] = '\0';

    for (int i = 0; DICT[i].name != NULL; i++) {
        if (strcmp(clean, DICT[i].name) == 0) {
            *group = DICT[i].group;
            *elem = DICT[i].elem;
            return 1;
        }
    }

    if (sscanf(tag_str, " ( %x , %x )", &g, &e) == 2) { *group = (uint16_t)g; *elem = (uint16_t)e; return 1; }
    if (sscanf(tag_str, " 0x%x , 0x%x", &g, &e) == 2) { *group = (uint16_t)g; *elem = (uint16_t)e; return 1; }
    if (sscanf(tag_str, " %x , %x", &g, &e) == 2) { *group = (uint16_t)g; *elem = (uint16_t)e; return 1; }
    if (strlen(clean) == 8 && sscanf(clean, "%04x%04x", &g, &e) == 2) {
        *group = (uint16_t)g; *elem = (uint16_t)e; return 1;
    }

    return 0;
}

EditRule* find_rule(EditRule *rules, int num_rules, uint16_t group, uint16_t elem) {
    for (int i = 0; i < num_rules; i++) {
        if (rules[i].group == group && rules[i].elem == elem)
            return &rules[i];
    }
    return NULL;
}

int compare_rules(const void *a, const void *b) {
    EditRule *r1 = (EditRule *)a;
    EditRule *r2 = (EditRule *)b;
    if (r1->group != r2->group) return r1->group - r2->group;
    return r1->elem - r2->elem;
}

void print_usage(const char* prog) {
    printf("Usage: %s [-d:DELIM] <input.dcm> [output.dcm] \"RULES\"\n\n", prog);
    printf("Examples:\n");
    printf("  %s in.dcm out.dcm \"PatientName=Doe\\^John\\xE2\\x80\\xA200100020=12345\"\n", prog);
    printf("  %s -d:,, in.dcm \"Modality=CT,,StudyDate=20261017,,(0008,103E)=Chest\"\n", prog);
    printf("\nNote: If [output.dcm] is omitted, the original file is overwritten.\n");
}

int main(int argc, char *argv[]) {
    char delim[64] = "\xE2\x80\xA2"; // Default delimiter: • (UTF-8 bullet)
    char *input_file = NULL;
    char *output_file = NULL;
    char *rules_str = NULL;
    int overwrite = 0;

    int pos_idx = 0;
    char *pos_args[3] = {NULL, NULL, NULL};

    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "-d:", 3) == 0) {
            strncpy(delim, argv[i] + 3, sizeof(delim)-1);
            delim[sizeof(delim)-1] = '\0';
            unescape_string(delim);
        } else {
            if (pos_idx < 3) {
                pos_args[pos_idx++] = argv[i];
            }
        }
    }

    if (pos_idx == 2) {
        input_file = pos_args[0];
        output_file = "dicom_temp_out.dcm";
        rules_str = pos_args[1];
        overwrite = 1;
    } else if (pos_idx == 3) {
        input_file = pos_args[0];
        output_file = pos_args[1];
        rules_str = pos_args[2];
    } else {
        print_usage(argv[0]);
        return 1;
    }

    EditRule rules[MAX_RULES];
    int num_rules = 0;

    char *args_copy = strdup(rules_str);
    char *p = args_copy;
    size_t delim_len = strlen(delim);

    while (p && *p && num_rules < MAX_RULES) {
        char *next = strstr(p, delim);
        if (next) {
            *next = '\0';
            next += delim_len;
        }
        
        char *eq = strchr(p, '=');
        if (eq) {
            *eq = '\0';
            char *tag_str = p;
            char *val_str = eq + 1;
            
            uint16_t g, e;
            if (parse_tag(tag_str, &g, &e)) {
                rules[num_rules].group = g;
                rules[num_rules].elem = e;
                rules[num_rules].new_val = strdup(val_str);
                rules[num_rules].applied = 0;
                unescape_string(rules[num_rules].new_val);
                num_rules++;
            } else {
                printf("Warning: Could not parse tag '%s'\n", tag_str);
            }
        }
        p = next;
    }
    free(args_copy);

    // Sort rules to ensure orderly injection
    qsort(rules, num_rules, sizeof(EditRule), compare_rules);

    FILE *fin = fopen(input_file, "rb");
    if (!fin) { printf("Failed to open input file: %s\n", input_file); return 1; }
    
    FILE *fout = fopen(output_file, "wb");
    if (!fout) { printf("Failed to open output file: %s\n", output_file); fclose(fin); return 1; }

    char header[132];
    if (fread(header, 1, 132, fin) != 132 || strncmp(header + 128, "DICM", 4) != 0) {
        printf("Not a valid DICOM file.\n");
        fclose(fin); fclose(fout); 
        if (overwrite) remove(output_file);
        return 1;
    }
    fwrite(header, 1, 132, fout);

    int in_meta = 1;
    int is_explicit = 1; 
    int ts_explicit = 1;

    uint16_t group, elem;
    while (fread(&group, 2, 1, fin) == 1) {
        fread(&elem, 2, 1, fin);

        if (in_meta && group > 0x0002) {
            in_meta = 0;
            is_explicit = ts_explicit; 
        }

        int is_sq_item = (group == 0xFFFE); 

        // Inject unapplied rules that strictly precede the current tag
        if (!is_sq_item) {
            for (int i = 0; i < num_rules; i++) {
                if (!rules[i].applied) {
                    if ((rules[i].group < group) || 
                        (rules[i].group == group && rules[i].elem < elem)) {
                        
                        int use_explicit = (rules[i].group <= 0x0002) ? ts_explicit : is_explicit;
                        inject_tag(fout, &rules[i], use_explicit);
                        rules[i].applied = 1;
                    }
                }
            }
        }

        char vr[3] = {0};
        uint32_t length = 0;
        int is_32bit_len = 0;

        if (is_explicit && !is_sq_item) {
            fread(vr, 1, 2, fin);
            if (memcmp(vr, "OB", 2)==0 || memcmp(vr, "OW", 2)==0 || memcmp(vr, "OF", 2)==0 ||
                memcmp(vr, "SQ", 2)==0 || memcmp(vr, "UT", 2)==0 || memcmp(vr, "UN", 2)==0 ||
                memcmp(vr, "UR", 2)==0 || memcmp(vr, "UC", 2)==0) {
                uint16_t reserved;
                fread(&reserved, 2, 1, fin);
                fread(&length, 4, 1, fin);
                is_32bit_len = 1;
            } else {
                uint16_t len16;
                fread(&len16, 2, 1, fin);
                length = len16;
            }
        } else {
            fread(&length, 4, 1, fin);
            is_32bit_len = 1;
        }

        EditRule *rule = find_rule(rules, num_rules, group, elem);

        fwrite(&group, 2, 1, fout);
        fwrite(&elem, 2, 1, fout);

        if (rule && !is_sq_item) {
            rule->applied = 1;
            uint32_t new_len = (uint32_t)strlen(rule->new_val);
            int needs_pad = (new_len % 2 != 0);
            if (needs_pad) new_len++;

            if (is_explicit) {
                fwrite(vr, 1, 2, fout);
                if (is_32bit_len) {
                    uint16_t res = 0;
                    fwrite(&res, 2, 1, fout);
                    fwrite(&new_len, 4, 1, fout);
                } else {
                    uint16_t l16 = (uint16_t)new_len;
                    fwrite(&l16, 2, 1, fout);
                }
            } else {
                fwrite(&new_len, 4, 1, fout);
            }

            fwrite(rule->new_val, 1, strlen(rule->new_val), fout);
            if (needs_pad) {
                char pad = (memcmp(vr, "UI", 2) == 0) ? '\0' : ' '; 
                fwrite(&pad, 1, 1, fout);
            }

            if (length > 0 && length != 0xFFFFFFFF) {
                fseek(fin, length, SEEK_CUR);
            }
        } else {
            if (is_explicit && !is_sq_item) {
                fwrite(vr, 1, 2, fout);
                if (is_32bit_len) {
                    uint16_t res = 0;
                    fwrite(&res, 2, 1, fout);
                    fwrite(&length, 4, 1, fout);
                } else {
                    uint16_t l16 = (uint16_t)length;
                    fwrite(&l16, 2, 1, fout);
                }
            } else {
                fwrite(&length, 4, 1, fout);
            }

            if (group == 0x0002 && elem == 0x0010 && length > 0) {
                char ts[128] = {0};
                fread(ts, 1, (length < sizeof(ts) ? length : sizeof(ts)-1), fin);
                fwrite(ts, 1, length, fout); 
                
                if (strncmp(ts, "1.2.840.10008.1.2.1", 19) == 0) {
                    ts_explicit = 1;
                } else if (strncmp(ts, "1.2.840.10008.1.2", 17) == 0) {
                    ts_explicit = 0; 
                }
            } 
            else if (length > 0 && length != 0xFFFFFFFF) {
                char buffer[8192];
                uint32_t remaining = length;
                while (remaining > 0) {
                    uint32_t to_read = remaining > sizeof(buffer) ? sizeof(buffer) : remaining;
                    size_t bytes = fread(buffer, 1, to_read, fin);
                    if (bytes == 0) break;
                    fwrite(buffer, 1, bytes, fout);
                    remaining -= (uint32_t)bytes;
                }
            }
        }
    }

    // Flush any remaining unapplied rules at the end of the file 
    for (int i = 0; i < num_rules; i++) {
        if (!rules[i].applied) {
            int use_explicit = (rules[i].group <= 0x0002) ? ts_explicit : is_explicit;
            inject_tag(fout, &rules[i], use_explicit);
            rules[i].applied = 1;
        }
    }

    fclose(fin);
    fclose(fout);
    for (int i = 0; i < num_rules; i++) free(rules[i].new_val);
    
    if (overwrite) {
        if (remove(input_file) != 0) {
            printf("Error: Could not overwrite original file. Output saved to %s\n", output_file);
        } else if (rename(output_file, input_file) != 0) {
            printf("Error: Could not rename temp file. Output saved to %s\n", output_file);
        } else {
            printf("Original DICOM overwritten successfully.\n");
        }
    } else {
        printf("DICOM modified successfully.\n");
    }

    return 0;
}
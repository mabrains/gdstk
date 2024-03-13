/*
Copyright 2020 Lucas Heitzmann Gabrielli.
This file is part of gdstk, distributed under the terms of the
Boost Software License - Version 1.0.  See the accompanying
LICENSE file or <http://www.boost.org/LICENSE_1_0.txt>
*/

#define __STDC_FORMAT_MACROS 1
#define _USE_MATH_DEFINES

#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <vector>
#include <iostream>

#include <gdstk/allocator.hpp>
#include <gdstk/gdsii.hpp>
#include <gdstk/rawcell.hpp>

namespace gdstk {

void RawCell::print(bool all) const {
    if (source) {
        printf("RawCell <%p>, %s, size %" PRIu64 ", source offset %" PRIu64 ", owner <%p>\n", this,
               name, size, offset, owner);
    } else {
        printf("RawCell <%p>, %s, size %" PRIu64 ", data <%p>, owner <%p>\n", this, name, size,
               data, owner);
    }
    if (all) {
        printf("Dependencies (%" PRIu64 "/%" PRIu64 "):\n", dependencies.count,
               dependencies.capacity);
        for (uint64_t i = 0; i < dependencies.count; i++) {
            printf("Dependency %" PRIu64 "", i);
            dependencies[i]->print(false);
        }
    }
}

void RawCell::clear() {
    if (name) {
        free_allocation(name);
        name = NULL;
    }
    if (source) {
        source->uses--;
        if (source->uses == 0) {
            fclose(source->file);
            free_allocation(source);
        }
        source = NULL;
        offset = 0;
    } else if (data) {
        free_allocation(data);
        data = NULL;
    }
    size = 0;
    dependencies.clear();
}

void RawCell::get_dependencies(bool recursive, Map<RawCell*>& result) const {
    RawCell** r_item = dependencies.items;
    for (uint64_t i = 0; i < dependencies.count; i++) {
        RawCell* rawcell = *r_item++;
        if (recursive && result.get(rawcell->name) != rawcell) {
            rawcell->get_dependencies(true, result);
        }
        result.set(rawcell->name, rawcell);
    }
}

void RawCell::get_polygons(int& start_id, int64_t depth, double unit, double tolerance, ErrorCode* error_code, std::vector<std::vector<int>>& polygons) {
    const char* gdsii_record_names[] = {
        "HEADER",    "BGNLIB",   "LIBNAME",   "UNITS",      "ENDLIB",      "BGNSTR",
        "STRNAME",   "ENDSTR",   "BOUNDARY",  "PATH",       "SREF",        "AREF",
        "TEXT",      "LAYER",    "DATATYPE",  "WIDTH",      "XY",          "ENDEL",
        "SNAME",     "COLROW",   "TEXTNODE",  "NODE",       "TEXTTYPE",    "PRESENTATION",
        "SPACING",   "STRING",   "STRANS",    "MAG",        "ANGLE",       "UINTEGER",
        "USTRING",   "REFLIBS",  "FONTS",     "PATHTYPE",   "GENERATIONS", "ATTRTABLE",
        "STYPTABLE", "STRTYPE",  "ELFLAGS",   "ELKEY",      "LINKTYPE",    "LINKKEYS",
        "NODETYPE",  "PROPATTR", "PROPVALUE", "BOX",        "BOXTYPE",     "PLEX",
        "BGNEXTN",   "ENDEXTN",  "TAPENUM",   "TAPECODE",   "STRCLASS",    "RESERVED",
        "FORMAT",    "MASK",     "ENDMASKS",  "LIBDIRSIZE", "SRFNAME",     "LIBSECUR"};

    // One extra char in case we need a 0-terminated string with max count (should never happen, but
    // it doesn't hurt to be prepared).
    uint8_t buffer[65537];
    int16_t* data16 = (int16_t*)(buffer + 4);
    int32_t* data32 = (int32_t*)(buffer + 4);
    uint64_t* data64 = (uint64_t*)(buffer + 4);
    char* str = (char*)(buffer + 4);

    double factor = 1;
    double width = 0;
    int16_t key = 0;

    bool target_cell_found = false;
    bool found_polygon = false;
    RawSource* source = (RawSource*)allocate(sizeof(RawSource));
    source->uses = 0;
    const char * c = filename.c_str();
    source->file = fopen(c, "rb");
    if (source->file == NULL) {
        if (error_logger) fputs("[GDSTK] Unable to open input GDSII file.\n", error_logger);
        if (error_code) *error_code = ErrorCode::InputFileOpenError;
        return;
    }

    while (true) {
        uint64_t record_length = COUNT(buffer);
        ErrorCode err = gdsii_read_record(source->file, buffer, record_length);
        if (err != ErrorCode::NoError) {
            if (error_code) *error_code = err;
            break;
        }

        uint64_t data_length;
        switch ((GdsiiDataType)buffer[3]) {
            case GdsiiDataType::BitArray:
            case GdsiiDataType::TwoByteSignedInteger:
                data_length = (record_length - 4) / 2;
                big_endian_swap16((uint16_t*)data16, data_length);
                break;
            case GdsiiDataType::FourByteSignedInteger:
            case GdsiiDataType::FourByteReal:
                data_length = (record_length - 4) / 4;
                big_endian_swap32((uint32_t*)data32, data_length);
                break;
            case GdsiiDataType::EightByteReal:
                data_length = (record_length - 4) / 8;
                big_endian_swap64(data64, data_length);
                break;
            default:
                data_length = record_length - 4;
        }

        switch ((GdsiiRecord)(buffer[2])) {
            case GdsiiRecord::LIBNAME:
                if (str[data_length - 1] == 0) data_length--;
                break;
            case GdsiiRecord::UNITS: {
                const double db_in_user = gdsii_real_to_double(data64[0]);
                const double db_in_meters = gdsii_real_to_double(data64[1]);
                if (unit > 0) {
                    factor = db_in_meters / unit;
                } else {
                    factor = db_in_user;
                }
                if (tolerance <= 0) {
                    tolerance = db_in_meters / factor;
                }
            } break;
            case GdsiiRecord::ENDLIB:
                return;
            case GdsiiRecord::STRNAME:
                if (strncmp(str, name, data_length) == 0) {
                    target_cell_found = true;
                    std::cout<<"target_cell_found: "<<name<<std::endl;
                }
                break;
            case GdsiiRecord::SREF:
            case GdsiiRecord::AREF:
                if (target_cell_found) {
                    // First, get the dependencies recursively
                    if(depth != 0){                    
                        Map<RawCell*> dependency_map = {};
                        get_dependencies(true, dependency_map);
                        // Iterate over the dependency map and call get_polygons on each element
                        if (dependency_map.has_key(str))
                        {
                            RawCell* ref = dependency_map.get_slot(str)->value;

                            std::cout<<"dependency name: "<<ref->name<<"\nstart_id: "<<start_id<<std::endl;
                            ref->get_polygons(start_id, -1, unit, tolerance, error_code, polygons);
                        }
                    }
                }
                break;
            
            case GdsiiRecord::BOUNDARY:
            case GdsiiRecord::BOX:
                if (target_cell_found) {
                    found_polygon = true;
                    std::cout<<"found_polygon: "<< " id:" <<start_id<<std::endl;
                }
                break;
            case GdsiiRecord::XY:
                if(found_polygon){
                  for (uint64_t i = 0; i < data_length; i += 4) {
                        int32_t x = factor * data32[i];
                        int32_t y = factor * data32[i + 1];
                        polygons.push_back(std::vector<int>{x,y,start_id});
                    }
                    start_id++;  // Increment the polygon_id for the next polygon
                }
                break;
            case GdsiiRecord::ENDEL:
                if (target_cell_found) {
                    found_polygon = false;
                }
                break;
            case GdsiiRecord::ENDSTR:
                // if (target_cell_found) {
                //     // First, get the dependencies recursively
                //     if(depth != 0){                    
                //         Map<RawCell*> dependency_map = {};
                //         get_dependencies(true, dependency_map);
                //         // Iterate over the dependency map and call get_polygons on each element
                //         MapItem<RawCell*>* item = dependency_map.next(NULL);
                //         while (item) {
                //             RawCell* rawcell = item->value;
                //             std::cout<<"dependency name: "<<rawcell->name<<"\nstart_id: "<<start_id<<std::endl;
                //             rawcell->get_polygons(start_id, -1, unit, tolerance, error_code, polygons);
                //             item = dependency_map.next(item);
                //         }
                //     }
                //     return;
                // }
                break;
            default:
                // Ignoring other records
                break;
        }
    }
}  



ErrorCode RawCell::to_gds(FILE* out) {
    ErrorCode error_code = ErrorCode::NoError;
    if (source) {
        uint64_t off = offset;
        data = (uint8_t*)allocate(size);
        int64_t result = source->offset_read(data, size, off);
        if (result < 0 || (uint64_t)result != size) {
            if (error_logger)
                fputs("[GDSTK] Unable to read RawCell data form input file.\n", error_logger);
            error_code = ErrorCode::InputFileError;
            size = 0;
        }
        source->uses--;
        if (source->uses == 0) {
            fclose(source->file);
            free_allocation(source);
        }
        source = NULL;
    }
    fwrite(data, 1, size, out);
    return error_code;
}

Map<RawCell*>  read_rawcells(const char* filename, ErrorCode* error_code) {
    Map<RawCell*> result = {};
    uint8_t buffer[65537];
    char* str = (char*)(buffer + 4);

    RawSource* source = (RawSource*)allocate(sizeof(RawSource));
    source->uses = 0;
    source->file = fopen(filename, "rb");
    if (source->file == NULL) {
        if (error_logger) fputs("[GDSTK] Unable to open input GDSII file.\n", error_logger);
        if (error_code) *error_code = ErrorCode::InputFileOpenError;
        return result;
    }

    RawCell* rawcell = NULL;

    while (true) {
        uint64_t record_length = COUNT(buffer);
        ErrorCode err = gdsii_read_record(source->file, buffer, record_length);
        if (err != ErrorCode::NoError) {
            if (error_code) *error_code = err;
            break;
        }

        switch (buffer[2]) {
            case 0x04: {  // ENDLIB
                for (MapItem<RawCell*>* item = result.next(NULL); item; item = result.next(item)) {
                    Array<RawCell*>* dependencies = &item->value->dependencies;
                    for (uint64_t i = 0; i < dependencies->count;) {
                        char* name = (char*)((*dependencies)[i]);
                        rawcell = result.get(name);
                        if (rawcell) {
                            if (dependencies->contains(rawcell)) {
                                dependencies->remove_unordered(i);
                            } else {
                                (*dependencies)[i] = rawcell;
                                i++;
                            }
                        } else {
                            dependencies->remove_unordered(i);
                            if (error_logger)
                                fprintf(error_logger, "[GDSTK] Referenced cell %s not found.\n",
                                        name);
                            if (error_code) *error_code = ErrorCode::MissingReference;
                        }
                        free_allocation(name);
                    }
                }
                if (source->uses == 0) {
                    fclose(source->file);
                    free_allocation(source);
                }
                return result;
            } break;
            case 0x05:  // BGNSTR
                rawcell = (RawCell*)allocate_clear(sizeof(RawCell));
                rawcell->source = source;
                source->uses++;
                rawcell->offset = ftell(source->file) - record_length;
                rawcell->size = record_length;
                rawcell->filename = std::string(filename);
                break;
            case 0x06:  // STRNAME
                if (rawcell) {
                    uint32_t data_length = (uint32_t)(record_length - 4);
                    if (str[data_length - 1] == 0) data_length--;
                    rawcell->name = (char*)allocate(data_length + 1);
                    memcpy(rawcell->name, str, data_length);
                    rawcell->name[data_length] = 0;
                    result.set(rawcell->name, rawcell);
                    rawcell->size += record_length;
                }
                break;
            case 0x07:  // ENDSTR
                if (rawcell) {
                    rawcell->size += record_length;
                    rawcell = NULL;
                }
                break;
            case 0x12:  // SNAME
                if (rawcell) {
                    uint32_t data_length = (uint32_t)(record_length - 4);
                    if (str[data_length - 1] == 0) data_length--;
                    char* name = (char*)allocate(data_length + 1);
                    memcpy(name, str, data_length);
                    name[data_length] = 0;
                    rawcell->dependencies.append((RawCell*)name);
                    rawcell->size += record_length;
                }
                break;
            default:
                if (rawcell) rawcell->size += record_length;
        }
    }

    source->uses++;  // ensure rawcell->clear() won't close and free source
    for (MapItem<RawCell*>* item = result.next(NULL); item; item = result.next(item)) {
        rawcell = item->value;
        Array<RawCell*>* dependencies = &rawcell->dependencies;
        for (uint64_t i = 0; i < dependencies->count;) {
            char* name = (char*)((*dependencies)[i]);
            free_allocation(name);
        }
        rawcell->clear();
    }
    fclose(source->file);
    free_allocation(source);
    result.clear();
    if (error_logger) fprintf(error_logger, "[GDSTK] Invalid GDSII file %s.\n", filename);
    if (error_code) *error_code = ErrorCode::InvalidFile;
    return result;
}

}  // namespace gdstk

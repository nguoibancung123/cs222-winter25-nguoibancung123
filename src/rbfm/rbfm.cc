#include "src/include/rbfm.h"
#include <climits>
#include <cstring>
#include "math.h"

namespace PeterDB
{
    PagedFileManager &_pf_manager = PagedFileManager::instance();

    RecordBasedFileManager &RecordBasedFileManager::instance()
    {
        static RecordBasedFileManager _rbf_manager = RecordBasedFileManager();
        return _rbf_manager;
    }

    RecordBasedFileManager::RecordBasedFileManager() = default;

    RecordBasedFileManager::~RecordBasedFileManager() = default;

    RecordBasedFileManager::RecordBasedFileManager(const RecordBasedFileManager &) = default;

    RecordBasedFileManager &RecordBasedFileManager::operator=(const RecordBasedFileManager &) = default;

    RC RecordBasedFileManager::createFile(const std::string &fileName)
    {
        return _pf_manager.createFile(fileName);
    }

    RC RecordBasedFileManager::destroyFile(const std::string &fileName)
    {
        return _pf_manager.destroyFile(fileName);
    }

    RC RecordBasedFileManager::openFile(const std::string &fileName, FileHandle &fileHandle)
    {
        return _pf_manager.openFile(fileName, fileHandle);
    }

    RC RecordBasedFileManager::closeFile(FileHandle &fileHandle)
    {
        return _pf_manager.closeFile(fileHandle);
    }

    RC RecordBasedFileManager::insertRecord(FileHandle &fileHandle, const std::vector<Attribute> &recordDescriptor,
                                            const void *inputData, RID &recordId)
    {
        // Number of fields in the record descriptor
        int numFields = recordDescriptor.size();
        printf("%d", numFields);

        // Size of null indicator and initial offsets for data processing
        int nullIndicatorSize = ceil((double)numFields / CHAR_BIT);
        int inputOffset = nullIndicatorSize; // Offset for field data in the input

        // Offsets for record memory
        int directoryOffset = nullIndicatorSize + sizeof(int);
        int dataStartOffset = directoryOffset + numFields * sizeof(int);

        // Calculate the maximum size of the record data
        int maxRecordSize = 0;
        for (int i = 0; i < numFields; i++)
        {
            maxRecordSize += recordDescriptor[i].length;
            if (recordDescriptor[i].type == TypeVarChar)
            {
                maxRecordSize += sizeof(int); // Additional space for string length
            }
        }

        // Allocate memory for the record
        unsigned char *recordBuffer = (unsigned char *)malloc(maxRecordSize + dataStartOffset);

        const char *inputBytes = (char *)inputData;    // Pointer to input data
        const char *nullIndicator = (char *)inputData; // Null indicator at the start of the input

        // Copy number of fields and null indicator to the record
        memcpy(recordBuffer, &numFields, sizeof(int));
        memcpy(recordBuffer + sizeof(int), inputBytes, nullIndicatorSize);

        // Process each field
        for (int i = 0; i < numFields; i++)
        {
            if (!(nullIndicator[i / 8] & (1 << (7 - i % 8))))
            {
                switch (recordDescriptor[i].type)
                {
                case TypeInt:
                    memcpy(recordBuffer + dataStartOffset, &inputBytes[inputOffset], sizeof(int));
                    inputOffset += sizeof(int);
                    dataStartOffset += sizeof(int);
                    memcpy(recordBuffer + directoryOffset, &dataStartOffset, sizeof(int));
                    directoryOffset += sizeof(int);
                    break;
                case TypeReal:
                    memcpy(recordBuffer + dataStartOffset, &inputBytes[inputOffset], sizeof(float));
                    inputOffset += sizeof(float);
                    dataStartOffset += sizeof(float);
                    memcpy(recordBuffer + directoryOffset, &dataStartOffset, sizeof(int));
                    directoryOffset += sizeof(int);
                    break;
                case TypeVarChar:
                    int strLength;
                    memcpy(&strLength, &inputBytes[inputOffset], sizeof(int));
                    memcpy(recordBuffer + dataStartOffset, &inputBytes[inputOffset + sizeof(int)], strLength);
                    dataStartOffset += strLength;
                    inputOffset += sizeof(int) + strLength;
                    memcpy(recordBuffer + directoryOffset, &dataStartOffset, sizeof(int));
                    directoryOffset += sizeof(int);
                    break;
                default:
                    break;
                }
            }
            else
            {
                // Null field; record current offset in the directory
                memcpy(recordBuffer + directoryOffset, &dataStartOffset, sizeof(int));
                directoryOffset += sizeof(int);
            }
        }

        // Handling pages in the file
        int currentPage = fileHandle.getNumberOfPages() - 1;
        int slotCount;
        int usedSpace;
        char *pageBuffer = (char *)malloc(PAGE_SIZE * sizeof(char));
        bool isFullCycle = false;

        if (currentPage < 0)
        {
            // No pages; create the first page and add the record
            memcpy(pageBuffer, recordBuffer, dataStartOffset);
            slotCount = 1;
            int directoryStart = PAGE_SIZE - sizeof(int) * 2 - sizeof(int) * 2;
            int recordStart = 0;
            memcpy(pageBuffer + directoryStart, &recordStart, sizeof(int));
            memcpy(pageBuffer + directoryStart + sizeof(int), &dataStartOffset, sizeof(int));
            usedSpace = dataStartOffset;
            memcpy(pageBuffer + PAGE_SIZE - sizeof(int) * 2, &slotCount, sizeof(int));
            memcpy(pageBuffer + PAGE_SIZE - sizeof(int), &usedSpace, sizeof(int));
            fileHandle.appendPage(pageBuffer);
            currentPage++;
        }
        else
        {
            while (currentPage < fileHandle.getNumberOfPages())
            {
                fileHandle.readPage(currentPage, pageBuffer);
                memcpy(&usedSpace, &pageBuffer[PAGE_SIZE - sizeof(int)], sizeof(int));
                memcpy(&slotCount, &pageBuffer[PAGE_SIZE - sizeof(int) * 2], sizeof(int));

                int directoryStart = PAGE_SIZE - sizeof(int) * 2 - slotCount * sizeof(int) * 2;
                if (directoryStart - usedSpace >= dataStartOffset + sizeof(int) * 2)
                {
                    // Add the record to the current page
                    memcpy(pageBuffer + usedSpace, recordBuffer, dataStartOffset);
                    slotCount++;
                    memcpy(pageBuffer + directoryStart - sizeof(int) * 2, &usedSpace, sizeof(int));
                    memcpy(pageBuffer + directoryStart - sizeof(int), &dataStartOffset, sizeof(int));
                    usedSpace += dataStartOffset;
                    memcpy(pageBuffer + PAGE_SIZE - sizeof(int) * 2, &slotCount, sizeof(int));
                    memcpy(pageBuffer + PAGE_SIZE - sizeof(int), &usedSpace, sizeof(int));
                    fileHandle.writePage(currentPage, pageBuffer);
                    break;
                }
                else
                {
                    if (currentPage == (fileHandle.getNumberOfPages() - 1) && !isFullCycle)
                    {
                        currentPage = 0;
                        isFullCycle = true;
                    }
                    else if (currentPage == (fileHandle.getNumberOfPages() - 1) && isFullCycle)
                    {
                        // No space in all pages; create a new page
                        memcpy(pageBuffer, recordBuffer, dataStartOffset);
                        slotCount = 1;
                        int directoryStart = PAGE_SIZE - sizeof(int) * 2 - sizeof(int) * 2;
                        int recordStart = 0;
                        memcpy(pageBuffer + directoryStart, &recordStart, sizeof(int));
                        memcpy(pageBuffer + directoryStart + sizeof(int), &dataStartOffset, sizeof(int));
                        usedSpace = dataStartOffset;
                        memcpy(pageBuffer + PAGE_SIZE - sizeof(int) * 2, &slotCount, sizeof(int));
                        memcpy(pageBuffer + PAGE_SIZE - sizeof(int), &usedSpace, sizeof(int));
                        fileHandle.appendPage(pageBuffer);
                        currentPage++;
                        break;
                    }
                    else
                    {
                        currentPage++;
                    }
                }
            }
        }

        // Set the record ID
        recordId.slotNum = slotCount;
        recordId.pageNum = currentPage;

        // Free allocated memory
        free(recordBuffer);
        free(pageBuffer);

        return 0;
    }

    RC RecordBasedFileManager::readRecord(FileHandle &fileHandle, const std::vector<Attribute> &recordDescriptor,
                                          const RID &recordID, void *outputData)
    {
        // Allocate memory to hold the page data
        char *pageData = (char *)malloc(PAGE_SIZE * sizeof(char));

        // Read the page containing the record into memory
        fileHandle.readPage(recordID.pageNum, pageData);

        // Determine the number of fields in the record
        int totalFields = recordDescriptor.size();

        // Retrieve the record's starting offset from the slot directory
        int recordStartOffset = 0;
        memcpy(&recordStartOffset,
               pageData + PAGE_SIZE - sizeof(int) * 2 - (2 * sizeof(int) * recordID.slotNum),
               sizeof(int));

        // Point to the actual record within the page
        char *recordPtr = pageData + recordStartOffset;

        // Determine the size of the null fields indicator
        int nullIndicatorSize = ceil((double)totalFields / CHAR_BIT);

        // Copy the null fields indicator to the output data
        memcpy(outputData, recordPtr + sizeof(int), nullIndicatorSize);

        // Initialize pointers and offsets for processing the record
        char *nullIndicator = recordPtr + sizeof(int);
        int outputDataOffset = nullIndicatorSize; // Start of the actual data in output
        int recordDataOffset = sizeof(int) + nullIndicatorSize + sizeof(int) * totalFields;
        int directoryOffset = sizeof(int) + nullIndicatorSize;

        char *outputDataPtr = (char *)outputData;

        // Initialize variables for directory and variable-length fields
        int previousFieldEnd = 0;
        int currentFieldEnd = 0;
        int varcharLength = 0;

        // Process each field in the record
        for (int fieldIndex = 0; fieldIndex < totalFields; fieldIndex++)
        {
            // Check if the field is null
            if (!(nullIndicator[fieldIndex / 8] & (1 << (7 - fieldIndex % 8))))
            {
                switch (recordDescriptor[fieldIndex].type)
                {
                case TypeInt:
                    // Copy integer data
                    memcpy(&outputDataPtr[outputDataOffset], recordPtr + recordDataOffset, sizeof(int));
                    outputDataOffset += sizeof(int);
                    recordDataOffset += sizeof(int);
                    break;

                case TypeReal:
                    // Copy real (float) data
                    memcpy(&outputDataPtr[outputDataOffset], recordPtr + recordDataOffset, sizeof(float));
                    outputDataOffset += sizeof(float);
                    recordDataOffset += sizeof(float);
                    break;

                case TypeVarChar:
                    // Determine the length of the variable-length string
                    if (fieldIndex == 0)
                        previousFieldEnd = recordDataOffset;
                    else
                        memcpy(&previousFieldEnd, recordPtr + directoryOffset + (fieldIndex - 1) * sizeof(int), sizeof(int));

                    memcpy(&currentFieldEnd, recordPtr + directoryOffset + fieldIndex * sizeof(int), sizeof(int));
                    varcharLength = currentFieldEnd - previousFieldEnd;

                    // Copy the length and the actual string data
                    memcpy(&outputDataPtr[outputDataOffset], &varcharLength, sizeof(int));
                    memcpy(&outputDataPtr[outputDataOffset + sizeof(int)], recordPtr + recordDataOffset, varcharLength);

                    outputDataOffset += sizeof(int) + varcharLength;
                    recordDataOffset += varcharLength;
                    break;

                default:
                    // Handle unexpected types (if necessary)
                    break;
                }
            }
        }

        // Free the allocated memory for the page data
        free(pageData);

        // Return success code
        return 0;
    }

    RC RecordBasedFileManager::deleteRecord(FileHandle &fileHandle, const std::vector<Attribute> &recordDescriptor,
                                            const RID &rid)
    {
        return -1;
    }

    RC RecordBasedFileManager::printRecord(const std::vector<Attribute> &fieldDescriptor, const void *recordData, std::ostream &out)
    {
        int totalFields = fieldDescriptor.size();
        int currentOffset = ceil((double)totalFields / CHAR_BIT);
        const char *dataPointer = (char *)recordData;

        // Iterate through all fields to print their content
        for (int fieldIndex = 0; fieldIndex < totalFields; fieldIndex++)
        {
            // Check if the field is null by evaluating the corresponding bit in the null bitmap
            if (!(dataPointer[fieldIndex / 8] & (1 << (7 - fieldIndex % 8))))
            {
                // Field is not null, process the field based on its type
                switch (fieldDescriptor[fieldIndex].type)
                {
                case TypeVarChar:
                {
                    // Handle variable-length string type
                    int strLength;
                    memcpy(&strLength, &dataPointer[currentOffset], sizeof(int));

                    // Allocate memory for the string and copy the content
                    char *stringContent = new char[strLength + 1];
                    memcpy(stringContent, &dataPointer[currentOffset + sizeof(int)], strLength);
                    stringContent[strLength] = '\0';

                    printf("%s: %s\n", fieldDescriptor[fieldIndex].name.c_str(), stringContent);

                    currentOffset += strLength + sizeof(int);
                    delete[] stringContent;
                    break;
                }
                case TypeInt:
                {
                    // Handle integer type
                    int intValue;
                    memcpy(&intValue, &dataPointer[currentOffset], sizeof(int));

                    printf("%s: %d\n", fieldDescriptor[fieldIndex].name.c_str(), intValue);
                    currentOffset += sizeof(int);
                    break;
                }
                case TypeReal:
                {
                    // Handle floating-point type
                    float floatValue;
                    memcpy(&floatValue, &dataPointer[currentOffset], sizeof(float));

                    printf("%s: %f\n", fieldDescriptor[fieldIndex].name.c_str(), floatValue);
                    currentOffset += sizeof(float);
                    break;
                }
                default:
                    break; // Handle unknown types if necessary
                }
            }
            else
            {
                // Field is null, print "NULL"
                printf("%s: NULL\n", fieldDescriptor[fieldIndex].name.c_str());
            }
        }

        return 0; // Return value to indicate success (could be improved for error handling)
    }

    RC RecordBasedFileManager::updateRecord(FileHandle &fileHandle, const std::vector<Attribute> &recordDescriptor,
                                            const void *data, const RID &rid)
    {
        return -1;
    }

    RC RecordBasedFileManager::readAttribute(FileHandle &fileHandle, const std::vector<Attribute> &recordDescriptor,
                                             const RID &rid, const std::string &attributeName, void *data)
    {
        return -1;
    }

    RC RecordBasedFileManager::scan(FileHandle &fileHandle, const std::vector<Attribute> &recordDescriptor,
                                    const std::string &conditionAttribute, const CompOp compOp, const void *value,
                                    const std::vector<std::string> &attributeNames,
                                    RBFM_ScanIterator &rbfm_ScanIterator)
    {
        return -1;
    }

} // namespace PeterDB

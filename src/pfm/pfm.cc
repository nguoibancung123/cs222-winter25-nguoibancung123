#include "src/include/pfm.h"
#include "stdio.h"
#include "unistd.h"
#include <stdlib.h>
#include <cstring>

namespace PeterDB
{
    PagedFileManager &PagedFileManager::instance()
    {
        static PagedFileManager _pf_manager = PagedFileManager();
        return _pf_manager;
    }

    PagedFileManager::PagedFileManager() = default;

    PagedFileManager::~PagedFileManager() = default;

    PagedFileManager::PagedFileManager(const PagedFileManager &) = default;

    PagedFileManager &PagedFileManager::operator=(const PagedFileManager &) = default;

    // Create a new file with the given name.
    // If the file already exists, return an error.
    RC PagedFileManager::createFile(const std::string &file_name)
    {
        // Check if the file already exists
        if (access(file_name.c_str(), F_OK) == 0)
        {
            perror("Error: File already exists. Cannot create a new file with the same name!");
            return -1;
        }

        // Attempt to open the file in write-binary mode
        FILE *new_file = fopen(file_name.c_str(), "wb");
        if (new_file == nullptr)
        {
            perror("Error: Failed to create the file!");
            return -1;
        }

        // Create a hidden metadata page in the newly created file
        FileHandle file_handle;
        file_handle.file_pointer = new_file;
        RC status = file_handle.initializeHiddenPage();

        // Close the file after creating the hidden page
        fclose(new_file);

        return status; // Return the status of hidden page creation
    }

    // Destroy the file with the given name.
    // If the file does not exist or cannot be removed, return an error.
    RC PagedFileManager::destroyFile(const std::string &file_name)
    {

        // Attempt to delete the file
        if (remove(file_name.c_str()) != 0)
        {
            perror("Error: Failed to destroy the file!");
            return -1;
        }

        puts("File successfully destroyed!");
        return 0; // Success
    }

    // Open an existing file with the given name and associate it with the provided FileHandle.
    // If the file does not exist or the FileHandle is already in use, return an error.
    RC PagedFileManager::openFile(const std::string &file_name, FileHandle &file_handle)
    {

        // Check if the FileHandle is already associated with another open file
        if (file_handle.file_pointer != nullptr)
        {
            perror("Error: FileHandle is already in use for another open file!");
        }

        // Attempt to open the file in read-write binary mode
        FILE *opened_file = fopen(file_name.c_str(), "rb+");
        if (opened_file == nullptr)
        {
            perror("Error: Failed to open the file!");
        }

        // Associate the opened file with the provided FileHandle
        file_handle.setOpenFile(opened_file);
        file_handle.setFileName(file_name);

        return 0; // Success
    }

    // Close the file associated with the given FileHandle.
    // If the FileHandle is not associated with an open file, return an error.
    RC PagedFileManager::closeFile(FileHandle &file_handle)
    {
        // Check if there is no file currently associated with the FileHandle
        if (file_handle.file_pointer == NULL)
        {
            perror("Error: No open file to close!");
            return -1;
        }

        // Close the file and reset the FileHandle pointer
        fclose(file_handle.file_pointer);
        file_handle.file_pointer = NULL;

        return 0; // Success
    }

    FileHandle::FileHandle()
    {
        readPageCounter = 0;
        writePageCounter = 0;
        appendPageCounter = 0;
    }

    FileHandle::~FileHandle() = default;

    // Reads the content of the specified page into the provided buffer.
    // If the page does not exist, returns an error.
    RC FileHandle::readPage(PageNum page_num, void *buffer)
    {
        PageNum total_pages = getNumberOfPages();

        // Check if the requested page exists
        if (page_num >= total_pages)
        {
            perror("Error: Attempting to read a non-existent page!");
            return -1;
        }

        // Seek to the page position, skipping the hidden page
        fseek(file_pointer, page_num * PAGE_SIZE + PAGE_SIZE, SEEK_SET);
        size_t read_bytes = fread(buffer, sizeof(char), PAGE_SIZE, file_pointer);

        // Verify if the read operation was successful
        if (read_bytes != PAGE_SIZE)
        {
            perror("Error: Failed to read page data!");
            return -1;
        }

        // Update the read page counter
        readPageCounter = getReadPageCount();
        readPageCounter++;
        setReadPageCount(readPageCounter);

        return 0; // Success
    }

    // Writes data to the specified page. If the page does not exist, returns an error.
    RC FileHandle::writePage(PageNum page_num, const void *buffer)
    {
        PageNum total_pages = getNumberOfPages();

        // Check if the page is valid
        if (page_num >= total_pages)
        {
            perror("Error: Attempting to write to a non-existent page!");
            return -1;
        }

        // Seek to the page position, skipping the hidden page
        fseek(file_pointer, page_num * PAGE_SIZE + PAGE_SIZE, SEEK_SET);
        size_t written_bytes = fwrite(buffer, sizeof(char), PAGE_SIZE, file_pointer);

        // Verify if the write operation was successful
        if (written_bytes != PAGE_SIZE)
        {
            perror("Error: Failed to write page data!");
            return -1;
        }

        // Flush the file to ensure data integrity
        if (fflush(file_pointer) != 0)
        {
            perror("Error: Failed to flush file after writing!");
            return -1;
        }

        // Update the write page counter
        writePageCounter = getWritePageCount();
        writePageCounter++;
        setWritePageCount(writePageCounter);

        return 0; // Success
    }

    // Appends a new page to the end of the file with the provided data.
    // Updates the total page count and counters accordingly.
    RC FileHandle::appendPage(const void *buffer)
    {
        fseek(file_pointer, 0, SEEK_END);
        size_t written_bytes = fwrite(buffer, sizeof(char), PAGE_SIZE, file_pointer);

        // Verify if the append operation was successful
        if (written_bytes != PAGE_SIZE)
        {
            perror("Error: Failed to append new page!");
            return -1;
        }

        // Flush the file to ensure data integrity
        if (fflush(file_pointer) != 0)
        {
            perror("Error: Failed to flush file after appending!");
            return -1;
        }

        // Update the append page counter
        appendPageCounter = getAppendPageCount();
        appendPageCounter++;
        setAppendPageCount(appendPageCounter);

        // Update the total number of pages
        PageNum total_pages = getNumberOfPages();
        total_pages++;
        setTotalPageCount(total_pages);

        return 0; // Success
    }

    // Function to retrieve the total number of pages from the file.
    // Returns the number of pages or 0 if an error occurs.
    unsigned FileHandle::getNumberOfPages()
    {
        fseek(file_pointer, 0, SEEK_SET); // Move file pointer to the beginning
        unsigned totalPageCount = 0;
        size_t bytesRead = fread(&totalPageCount, sizeof(unsigned), 1, file_pointer);

        if (bytesRead != 1)
        {
            perror("Error reading the total number of pages!");
            return 0; // Placeholder for error handling
        }
        return totalPageCount;
    }

    RC FileHandle::collectCounterValues(unsigned &readPageCount, unsigned &writePageCount, unsigned &appendPageCount)
    {
        readPageCount = readPageCounter;
        writePageCount = writePageCounter;
        appendPageCount = appendPageCounter;
        return 0;
    }

    // Function to retrieve the read page counter value from the file.
    // Returns the counter value or 0 if an error occurs.
    int FileHandle::getReadPageCount()
    {
        fseek(file_pointer, sizeof(unsigned), SEEK_SET); // Position after the total page count
        unsigned readCounter = 0;
        size_t bytesRead = fread(&readCounter, sizeof(unsigned), 1, file_pointer);

        if (bytesRead != 1)
        {
            perror("Error reading the read page counter!");
            return 0; // Placeholder for error handling
        }
        return readCounter;
    }

    // Function to write the read page counter value to the file.
    // Returns 0 on success, -1 on error.
    RC FileHandle::setReadPageCount(unsigned readPageCnt)
    {
        fseek(file_pointer, sizeof(unsigned), SEEK_SET); // Position after the total page count
        size_t bytesWritten = fwrite(&readPageCnt, sizeof(unsigned), 1, file_pointer);

        if (bytesWritten != 1)
        {
            perror("Error writing the read page counter!");
            return -1; // Error code
        }
        fflush(file_pointer); // Ensure changes are written to disk
        return 0;             // Success
    }

    // Function to retrieve the write page counter value from the file.
    int FileHandle::getWritePageCount()
    {
        fseek(file_pointer, sizeof(unsigned) * 2, SEEK_SET); // Position after total and read counters
        unsigned writeCounter = 0;
        size_t bytesRead = fread(&writeCounter, sizeof(unsigned), 1, file_pointer);

        if (bytesRead != 1)
        {
            perror("Error reading the write page counter!");
            return 0; // Placeholder for error handling
        }
        return writeCounter;
    }

    // Function to write the write page counter value to the file.
    RC FileHandle::setWritePageCount(unsigned writeCounter)
    {
        fseek(file_pointer, sizeof(unsigned) * 2, SEEK_SET); // Position after total and read counters
        size_t bytesWritten = fwrite(&writeCounter, sizeof(unsigned), 1, file_pointer);

        if (bytesWritten != 1)
        {
            perror("Error writing the write page counter!");
            return -1; // Error code
        }
        fflush(file_pointer);
        return 0; // Success
    }

    // Function to retrieve the append page counter value from the file.
    int FileHandle::getAppendPageCount()
    {
        fseek(file_pointer, sizeof(unsigned) * 3, SEEK_SET); // Position after total, read, and write counters
        unsigned appendCounter = 0;
        size_t bytesRead = fread(&appendCounter, sizeof(unsigned), 1, file_pointer);

        if (bytesRead != 1)
        {
            perror("Error reading the append page counter!");
            return 0; // Placeholder for error handling
        }
        return appendCounter;
    }

    // Function to write the append page counter value to the file.
    RC FileHandle::setAppendPageCount(unsigned appendCounter)
    {
        fseek(file_pointer, sizeof(unsigned) * 3, SEEK_SET); // Position after total, read, and write counters
        size_t bytesWritten = fwrite(&appendCounter, sizeof(unsigned), 1, file_pointer);

        if (bytesWritten != 1)
        {
            perror("Error writing the append page counter!");
            return -1; // Error code
        }
        fflush(file_pointer);
        return 0; // Success
    }

    // Function to write the total number of pages to the file.
    // Returns 0 on success, -1 on error.
    RC FileHandle::setTotalPageCount(unsigned totalPageCount)
    {
        fseek(file_pointer, 0, SEEK_SET); // Move file pointer to the beginning
        size_t bytesWritten = fwrite(&totalPageCount, sizeof(unsigned), 1, file_pointer);

        if (bytesWritten != 1)
        {
            perror("Error writing the total number of pages!");
            return -1; // Error code
        }
        fflush(file_pointer);
        return 0; // Success
    }

    // Function to initialize the hidden page with counter values and metadata.
    RC FileHandle::initializeHiddenPage()
    {
        void *hiddenPageData = malloc(PAGE_SIZE); // Allocate memory for hidden page
        if (!hiddenPageData)
        {
            perror("Memory allocation error for hidden page!");
            return -1;
        }

        unsigned totalPageCount = 0;
        unsigned readCounter = 0;
        unsigned writeCounter = 0;
        unsigned appendCounter = 0;
        int dataOffset = 0;

        // Pack counter values into the hidden page buffer
        memcpy(static_cast<char *>(hiddenPageData) + dataOffset, &totalPageCount, sizeof(unsigned));
        dataOffset += sizeof(unsigned);
        memcpy(static_cast<char *>(hiddenPageData) + dataOffset, &readCounter, sizeof(unsigned));
        dataOffset += sizeof(unsigned);
        memcpy(static_cast<char *>(hiddenPageData) + dataOffset, &writeCounter, sizeof(unsigned));
        dataOffset += sizeof(unsigned);
        memcpy(static_cast<char *>(hiddenPageData) + dataOffset, &appendCounter, sizeof(unsigned));

        // Write the hidden page to the file
        size_t bytesWritten = fwrite(hiddenPageData, sizeof(char), PAGE_SIZE, file_pointer);
        if (bytesWritten != PAGE_SIZE)
        {
            perror("Error writing the hidden page!");
            free(hiddenPageData);
            return -1;
        }

        fflush(file_pointer); // Ensure changes are written to disk
        free(hiddenPageData); // Free allocated memory
        return 0;             // Success
    }

    void FileHandle::setOpenFile(FILE *pFile)
    {
        file_pointer = pFile;
        readPageCounter = getReadPageCount();
        writePageCounter = getWritePageCount();
        appendPageCounter = getAppendPageCount();
    }

    FILE *FileHandle::getFile()
    {
        return file_pointer;
    }
    std::string FileHandle::getFileName()
    {
        return fileName;
    }

    void FileHandle::setFileName(const std::string &file_name)
    {
        this->fileName.assign(file_name);
    }

} // namespace PeterDB
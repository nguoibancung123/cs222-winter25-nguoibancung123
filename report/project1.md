## Project 1 Report


### 1. Basic information
 - Team #:
 - Github Repo Link:
 - Student 1 UCI NetID:
 - Student 1 Name:
 - Student 2 UCI NetID (if applicable):
 - Student 2 Name (if applicable):


### 2. Internal Record Format
- Show your record format design.
Our record format includes a fixed header followed by dynamically sized fields. Each record consists of:

1. A unique record ID.
2. Field count metadata for schema adherence.
3. Fixed-length fields, if applicable, followed by variable-length fields.


- Describe how you store a null field.
Null fields are represented using a null indicator bitmap within the record header. Each bit corresponds to a specific field, where 1 indicates a null value.


- Describe how you store a VarChar field.
VarChar fields are stored using a two-part structure:

1. A 2-byte integer specifying the length of the string.
2. The actual string content, which is stored in a contiguous block of bytes.


- Describe how your record design satisfies O(1) field access.
To achieve constant-time (O(1)) access to any field, the internal record format employs a fixed structure where each field is located at a known offset relative to the start of the record. This allows direct access to any field by calculating the offset based on the field's position in the record.


### 3. Page Format
- Show your page format design.
Each page comprises:

1. A header section that includes metadata such as page ID, free space offset, and record count.
2. A slot directory to store pointers to individual records.
3. The record area where actual data is stored.


- Explain your slot directory design if applicable.
The slot directory is a list of pointers to the records stored in the page. It enables efficient tracking of where each record is located within the page. Each slot in the directory points to the start of a record, and the directory allows for efficient management of records (insertion, deletion, etc.) within a page.


### 4. Page Management
- Show your algorithm of finding next available-space page when inserting a record.
Our algorithm uses the following steps:

1. Check the free space indicator for the current page.
2. If space is insufficient, iterate through a list of known pages to find one with available space.
3. If no page has sufficient space, create a new page and add it to the list.


- How many hidden pages are utilized in your design?

Our design utilizes one hidden page. This page serves as a metadata store, keeping track of key counters such as the total number of pages, the read operation count, the write operation count, and the append operation count.

- Show your hidden page(s) format design if applicable

The hidden page is initialized with the following structure:

1. Total Page Count (unsigned): 4 bytes used to store the total number of pages in the file.
2. Read Counter (unsigned): 4 bytes to track the number of read operations.
3. Write Counter (unsigned): 4 bytes to track the number of write operations.
4. Append Counter (unsigned): 4 bytes to track the number of append operations.

The counters are packed sequentially into the hidden page buffer and written to disk. The format ensures efficient storage and retrieval of metadata.

### 5. Implementation Detail

### 6. Member contribution (for team of two)
- Explain how you distribute the workload in team.



### 7. Other (optional)
- Freely use this section to tell us about things that are related to the project 1, but not related to the other sections (optional)



- Feedback on the project to help improve the project. (optional)
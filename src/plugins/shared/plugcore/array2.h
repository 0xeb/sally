// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// ****************************************************************************
// TDirectArray2:
//  -array that grows/shrinks dynamically in blocks (no need to reallocate
//   already used memory, only add another block)
//  -when deleting an element from the array, method Destructor(index_element) is called,
//   which in the base object does nothing

template <class DATA_TYPE>
class TDirectArray2
{
protected:
    DATA_TYPE** Blocks; // pointer to block array
    int BlockSize;      // size of one block

public:
    int Count; // number of elements in the array

    TDirectArray2<DATA_TYPE>(int blockSize)
    {
        Blocks = NULL;
        BlockSize = blockSize;
        Count = 0;
    }

    virtual ~TDirectArray2() { Destroy(); };

    virtual void Destructor(int) {}

    void Destroy();                    // clears the array
    BOOL Add(const DATA_TYPE& member); // adds an element at the last position
    BOOL Delete(int index);            // removes element at the given position, replaces it
                                       // with the last element and shrinks the array
                                       /*
    CDynamicArray * const &operator[](float index); // function is never called, but when it is not here
                                                    // MSVC does nasty things
*/
    DATA_TYPE& operator[](int index)   // returns element at position
    {
        return Blocks[index / BlockSize][index % BlockSize];
    }
};

// ****************************************************************************
// CArray2:
//  -base of all indirect arrays
//  -stores type (void *) in DWORD array (to save space in .exe)

class CArray2 : public TDirectArray2<DWORD_PTR>
{
protected:
    BOOL DeleteMembers;

public:
    CArray2(int blockSize, BOOL deleteMembers) : TDirectArray2<DWORD_PTR>(blockSize)
    {
        DeleteMembers = deleteMembers;
    }

    BOOL Add(const void* member)
    {
        return TDirectArray2<DWORD_PTR>::Add((DWORD_PTR)member);
    }

protected:
    virtual void Destructor(void* member) = 0;
    virtual void Destructor(int i) { Destructor((void*)Blocks[i / BlockSize][i % BlockSize]); }
};

// ****************************************************************************
// TIndirectArray2:
//  -suitable for storing pointers to objects
//  -other properties see CArray

template <class DATA_TYPE>
class TIndirectArray2 : public CArray2
{
public:
    TIndirectArray2(int blockSize, BOOL deleteMembers = TRUE) : CArray2(blockSize, deleteMembers) {}

    DATA_TYPE*& At(int index)
    {
        return (DATA_TYPE*&)(this->CArray::operator[](index));
    }

    DATA_TYPE*& operator[](int index)
    {
        return (DATA_TYPE*&)(CArray2::operator[](index));
    }

    virtual ~TIndirectArray2() { Destroy(); }

protected:
    virtual void Destructor(void* member)
    {
        if (DeleteMembers && member != NULL)
            delete ((DATA_TYPE*)member);
    }
};

// ****************************************************************************
//
// TDirectArray2
//

template <class DATA_TYPE>
void TDirectArray2<DATA_TYPE>::Destroy()
{
    if (Count)
    {
        for (int i = 0; i < Count; i++)
            Destructor(i);

        // if Count == BlockSize it caused problems
        // therefore Count - 1 is used here
        for (DATA_TYPE** block = Blocks; block <= Blocks + (Count - 1) / BlockSize; block++)
        {
            free(*block);
        }
        free(Blocks);
        Blocks = NULL;
        Count = 0;
    }
}

template <class DATA_TYPE>
int TDirectArray2<DATA_TYPE>::Add(const DATA_TYPE& member)
{
    //  CALL_STACK_MESSAGE1("CDynamicArray::Add( )");
    if (!(Count % BlockSize))
    {
        DATA_TYPE** newArrayBlocks;

        newArrayBlocks = (DATA_TYPE**)realloc(Blocks, (Count / BlockSize + 1) * sizeof(DATA_TYPE*));
        if (newArrayBlocks)
        {
            Blocks = newArrayBlocks;
            Blocks[Count / BlockSize] = (DATA_TYPE*)malloc(BlockSize * sizeof(DATA_TYPE));
            if (!Blocks[Count / BlockSize])
            {
                if (!Count)
                {
                    free(Blocks);
                    Blocks = NULL;
                }
                return FALSE;
            }
        }
        else
            return FALSE;
    }
    Blocks[Count / BlockSize][Count % BlockSize] = member;
    Count++;
    return TRUE;
}

template <class DATA_TYPE>
BOOL TDirectArray2<DATA_TYPE>::Delete(int index)
{
    if (index >= Count)
        return FALSE;
    Destructor(index);
    Count--;
    Blocks[index / BlockSize][index % BlockSize] = Blocks[Count / BlockSize][Count % BlockSize];
    if (!(Count % BlockSize))
    {
        free(Blocks[Count / BlockSize]);
    }
    if (!Count)
    {
        free(Blocks);
        Blocks = NULL;
    }
    return TRUE;
}

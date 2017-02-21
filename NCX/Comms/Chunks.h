/*
	File:		Chunks.h

	Contains:	Interface to buffered chunks of data.

	Written by:	Newton Research Group, 2005.
*/


#define kChunkSize 1024

/* -----------------------------------------------------------------------------
	C C h u n k
----------------------------------------------------------------------------- */

class CChunk
{
public:
						CChunk();
						~CChunk();

	void				init(void);
	unsigned int	amtFilled(void);
	unsigned int	amtAvailable(void);
	bool				read(void * outBuf, unsigned int inSize);
	void				write(const void * inBuf, unsigned int inSize);

private:
	char	 	data[kChunkSize];
	char *	ptrIn;
	char *	ptrOut;
};


/* -----------------------------------------------------------------------------
	C C h u n k B u f f e r
----------------------------------------------------------------------------- */

class CChunkBuffer
{
public:
						CChunkBuffer();
						~CChunkBuffer();

	unsigned int	size(void);
	CChunk *			getNextChunk(void);
	unsigned int	read(void * outBuf, unsigned int inSize);
	unsigned int	write(const void * inBuf, unsigned int inSize);
	void				flush(void);

	int				nextChar(void);

private:
	unsigned int	numOfChunks;
	CChunk **		chunks;
};


// Copyright (c) 2016 The Shenzhen BitChain Technology Company
// Copyright (c) 2016 Vpncoin development team, Bit Lee

#include "wallet.h"
#include "walletdb.h"
#include "bitcoinrpc.h"
#include "init.h"
#include "base58.h"
#include "lz4/lz4.h"
#include "lzma/LzmaLib.h"
//#include "aes/Rijndael.h"
//#include "aes/aes.h"
//#include "bitchain.h"
#include <fstream>


#include <iostream>
#include <iterator>
#include <vector>

using namespace json_spirit;
using namespace std;
typedef char * PCHAR;

extern int dw_zip_block;
extern int dw_zip_limit_size;
unsigned int uint_256KB = 256 * 1024;

int StreamToBuffer(CDataStream &ds, string& sRzt, int iSaveBufSize)
{
	int bsz = ds.size();
	int iRsz = bsz;
	if( iSaveBufSize > 0 ){ iRsz = iRsz + 4; }
	sRzt.resize(iRsz);
	char* ppp = (char*)sRzt.c_str();
	if( iSaveBufSize > 0 ){ ppp = ppp + 4; }
	ds.read(ppp, bsz);
	if( iSaveBufSize > 0 ){ *(unsigned int *)(ppp - 4) = bsz; }
	return iRsz;
}
int CBlockToBuffer(CBlock *pb, string& sRzt)
{
	CDataStream ssBlock(SER_DISK, CLIENT_VERSION);
	ssBlock << (*pb);
	/*int bsz = ssBlock.size();
	sRzt.resize(bsz);
	char* ppp = (char*)sRzt.c_str();
	ssBlock.read(ppp, bsz);*/
	int bsz = StreamToBuffer(ssBlock, sRzt, 0);
	return bsz;
}
int writeBufToFile(char* pBuf, int bufLen, string fName)
{
	int rzt = 0;
	std::ofstream oFs(fName.c_str(), std::ios::out | std::ofstream::binary);
	if( oFs.is_open() )
	{
		if( pBuf ) oFs.write(pBuf, bufLen);
		oFs.close(); 
		rzt++; 
	}
	return rzt;
}

int lz4_pack_buf(char* pBuf, int bufLen, string& sRzt)
{
	int worstCase = 0;
	int lenComp = 0;
    try{
		worstCase = LZ4_compressBound( bufLen );
		//std::vector<uint8_t> vchCompressed;   //vchCompressed.resize(worstCase);
		sRzt.resize(worstCase + 4);
		char* pp = (char *)sRzt.c_str();
		lenComp = LZ4_compress(pBuf, pp + 4, bufLen); 
		if( lenComp > 0 ){ *(unsigned int *)pp = bufLen;   lenComp = lenComp + 4; }
	}
    catch (std::exception &e) {
        printf("lz4_pack_buf err [%s]:: buf len %d, worstCase[%d], lenComp[%d] \n", e.what(), bufLen, worstCase, lenComp);
    }
	return lenComp;
}

int lz4_unpack_buf(const char* pZipBuf, unsigned int zipLen, string& sRzt)
{
	int rzt = 0;
	unsigned int realSz = *(unsigned int *)pZipBuf;
	if( fDebug )printf("lz4_unpack_buf:: zipLen [%d], realSz [%d],  \n", zipLen, realSz);
	sRzt.resize(realSz);
	char* pOutData = (char*)sRzt.c_str();
	
    // -- decompress
	rzt = LZ4_decompress_safe(pZipBuf + 4, pOutData, zipLen, realSz);
    if ( rzt != (int) realSz)
    {
            if( fDebug )printf("lz4_unpack_buf:: Could not decompress message data. [%d :: %d] \n", rzt, realSz);
            sRzt.resize(0);
    }
	return rzt;
}
int CBlockFromBuffer(CBlock* block, char* pBuf, int bufLen)
{
    //vector<char> v(bufLen);
	//memcpy((char*)&v[0], pBuf, bufLen);
	CDataStream ssBlock(SER_DISK, CLIENT_VERSION);
	ssBlock.write(pBuf, bufLen);   int i = ssBlock.size();
	//ssBlock << v;   
	ssBlock >> (*block);
	return i;
}

int lz4_pack_block(CBlock* block, string& sRzt)
{
	int rzt = 0;
	string sbf;
	int bsz = CBlockToBuffer(block, sbf);
	if( bsz > 12 )
	{
		char* pBuf = (char*)sbf.c_str();
		rzt = lz4_pack_buf(pBuf, bsz, sRzt);
		//if( lzRzt > 0 ){ rzt = lzRzt; }  // + 4; }
	}
	sbf.resize(0);
	return rzt;
}

int lzma_depack_buf(unsigned char* pLzmaBuf, int bufLen, string& sRzt)
{
	int rzt = 0;
	unsigned int dstLen = *(unsigned int *)pLzmaBuf;
    sRzt.resize(dstLen);
	unsigned char* pOutBuf = (unsigned char*)sRzt.c_str();
    unsigned srcLen = bufLen - LZMA_PROPS_SIZE - 4;
	SRes res = LzmaUncompress(pOutBuf, &dstLen, &pLzmaBuf[LZMA_PROPS_SIZE + 4], &srcLen, &pLzmaBuf[4], LZMA_PROPS_SIZE);
	if( res == SZ_OK )//assert(res == SZ_OK);
	{
		//outBuf.resize(dstLen); // If uncompressed data can be smaller
		rzt = dstLen;
	}else sRzt.resize(0);
	if( fDebug ) printf("lzma_depack_buf:: res [%d], dstLen[%d],  rzt = [%d]\n", res, dstLen, rzt);
	return rzt;
}

int lzma_pack_buf(unsigned char* pBuf, int bufLen, string& sRzt, int iLevel, unsigned int iDictSize)  // (1 << 17) = 131072 = 128K
{
	int res = 0;
	int rzt = 0;
		unsigned propsSize = LZMA_PROPS_SIZE;
		unsigned destLen = bufLen + (bufLen / 3) + 128;
    try{
		sRzt.resize(propsSize + destLen + 4);
		unsigned char* pOutBuf = (unsigned char*)sRzt.c_str();

		res = LzmaCompress(&pOutBuf[LZMA_PROPS_SIZE + 4], &destLen, pBuf, bufLen, &pOutBuf[4], &propsSize,
                                      iLevel, iDictSize, -1, -1, -1, -1, -1);  // 1 << 14 = 16K, 1 << 16 = 64K
  
		//assert(propsSize == LZMA_PROPS_SIZE);
		//assert(res == SZ_OK);
		if( (res == SZ_OK) && (propsSize == LZMA_PROPS_SIZE) )  
		{
			//outBuf.resize(propsSize + destLen);
			*(unsigned int *)pOutBuf = bufLen; 
			rzt = propsSize + destLen + 4;
		}else sRzt.resize(0);
	
	}
    catch (std::exception &e) {
        printf("lzma_pack_buf err [%s]:: buf len %d, rzt[%d] \n", e.what(), bufLen, rzt);
    }
	if( fDebug ) printf("lzma_pack_buf:: res [%d], propsSize[%d], destLen[%d],  rzt = [%d]\n", res, propsSize, destLen, rzt);
	return rzt;
}

int lzma_pack_block(CBlock* block, string& sRzt, int iLevel, unsigned int iDictSize)  // (1 << 17) = 131072 = 128K
{
	int rzt = 0;
	string sbf;
	int bsz = CBlockToBuffer(block, sbf);
	if( bsz > 12 )
	{
		unsigned char* pBuf = (unsigned char*)sbf.c_str();
		rzt = lzma_pack_buf(pBuf, bsz, sRzt, iLevel, iDictSize);
		//if( lzRzt > 0 ){ rzt = lzRzt; }  // + 4; }
	}
	sbf.resize(0);
	return rzt;
}

void lzma_Block_to_file(CBlock *block, string sFile)
{
	string lzBuf;
	int64_t i61 = GetTimeMillis();
	int lzRzt = lzma_pack_block(block, lzBuf, 9, uint_256KB);
	int64_t i62 = GetTimeMillis();
	printf("lzma_Block_to_file:: lzma_pack_block used time (%I64d ~ %I64d) = [%I64d] \n", i61, i62, i62 - i61);
	if( lzRzt > 0 )
	{
		string sss = sFile + ".lzma";
		char* pBuf = (char*)lzBuf.c_str();
		writeBufToFile(pBuf, lzRzt, sss);
		
		string sDe;
		int lzRzt = lzma_depack_buf((unsigned char*)pBuf, lzRzt, sDe);
		if( lzRzt > 0 )
		{
			sss = sFile + ".unlzma";
			pBuf = (char*)sDe.c_str();
			writeBufToFile(pBuf, lzRzt, sss);
		}
	}
}

int bitnet_pack_block(CBlock* block, string& sRzt)
{
	if( dw_zip_block == 1 )  return lzma_pack_block(block, sRzt, 9, uint_256KB);
	else if( dw_zip_block == 2 ) return lz4_pack_block(block, sRzt);
}

 bool readZipBlockFromDisk(const CBlockIndex* pindex, std::string& sRzt)
{
    sRzt = "";   // bool fReadTransactions = true;
    if( !pindex ){ return false; }
	unsigned int fPos = 0;
	//if( dw_zip_block > 0 ){ nBlockPos = 0; }
    const CDiskBlockPos pos = pindex->GetBlockPos();

    // Open history file to read
    //CAutoFile filein = CAutoFile(OpenBlockFile(pindex->nFile, fPos, "rb"), SER_DISK, CLIENT_VERSION);
    CAutoFile filein = CAutoFile(OpenBlockFile(pos, true), SER_DISK, CLIENT_VERSION);
    if( !filein ){ return false; }  //error("CBlock::ReadFromDisk() : OpenBlockFile failed");
    //if( !fReadTransactions ){ filein.nType |= SER_BLOCKHEADERONLY; }

	int ips = pos.nPos - 4;   //pindex->nBlockPos - 4;  // get ziped block size;
	if (fseek(filein, ips, SEEK_SET) != 0)
		return false;   //error("getCBlockByFilePos:: fseek failed");
	filein >> ips;	// get ziped block size;
	sRzt.resize(ips);   char* pZipBuf = (char *)sRzt.c_str();
	filein.read(pZipBuf, ips);

    //if (!ReadFromDisk(pindex->nFile, pindex->nBlockPos, fReadTransactions)){ return false; }
    //if (GetHash() != pindex->GetBlockHash())
    //    return error("CBlock::ReadFromDisk() : GetHash() doesn't match index");
    return true;
}

bool getCBlockByFilePos(const CAutoFile& filein, unsigned int nBlockPos, CBlock* block)
{
	bool rzt = false;
	int ips = nBlockPos - 4;  // get ziped block size;
	if (fseek((CAutoFile&)filein, ips, SEEK_SET) != 0)
		return error("getCBlockByFilePos:: fseek failed");
	(CAutoFile&)filein >> ips;	// get ziped block size;
	if( fDebug )printf("getCBlockByFilePos:: ziped block size [%d] \n", ips);
	string s;   s.resize(ips);   char* pZipBuf = (char *)s.c_str();
	((CAutoFile&)filein).read(pZipBuf, ips);
	string sUnpak;
	int iRealSz;
	if( dw_zip_block == 1 ) iRealSz = lzma_depack_buf((unsigned char*)pZipBuf, ips, sUnpak);
	else if( dw_zip_block == 2 ) iRealSz = lz4_unpack_buf(pZipBuf, ips - 4, sUnpak);
	if( fDebug )printf("getCBlockByFilePos:: ziped block size [%d], iRealSz [%d] \n", ips, iRealSz);
	if( iRealSz > 0 )
	{
		pZipBuf = (char *)sUnpak.c_str();
		rzt = CBlockFromBuffer(block, pZipBuf, iRealSz) > 12;
		/*if( fDebug ){
			if( block->vtx.size() < 10 )
			{
				printf("\n\n getCBlockByFilePos:: block info (%d): \n", rzt);
				block->print(); 
			}else printf("\n\n getCBlockByFilePos:: block vtx count (%d) is too large \n", block->vtx.size());
		}*/
	}
	s.resize(0);   sUnpak.resize(0);
	return rzt;
}

bool getCBlocksTxByFilePos(const CAutoFile& filein, unsigned int nBlockPos, unsigned int txId, CTransaction& tx)
{
	bool rzt = false;
	CBlock block;
	rzt = getCBlockByFilePos(filein, nBlockPos, &block);
	if( rzt )
	{
		if( block.vtx.size() > txId )
		{
			tx = block.vtx[txId];
			if( fDebug ){
			printf("\n\n getCBlocksTxByFilePos:: tx info: \n");
			tx.print(); }
		}else rzt = false;
	}
	return rzt;
}

int bitnet_pack_buf(char* pBuf, int bufLen, string& sRzt)
{
    if( dw_zip_block == 1 ) return lzma_pack_buf((unsigned char *)pBuf, bufLen, sRzt, 9, uint_256KB);
	else if( dw_zip_block == 2 ) return lz4_pack_buf((char *)pBuf, bufLen, sRzt);
}
int bitnet_depack_buf(char* pLzBuf, int lzBufLen, string& sRzt)
{
    if( dw_zip_block == 1 ) return lzma_depack_buf((unsigned char *)pLzBuf, lzBufLen, sRzt);
	else if( dw_zip_block == 2 ) return lz4_unpack_buf(pLzBuf, lzBufLen, sRzt);
}

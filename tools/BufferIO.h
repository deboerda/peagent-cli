/******************************************************************************
* 版权所有(c) <武汉创景可视技术有限公司>, 保留所有权利
******************************************************************************/
#pragma once

#include "memory.h"
#include "VsMacros.h"
#include "VsMVOMacros.h"

#define BUF_SEEK_CUR	0
#define BUF_SEEK_SET	1
#define BUF_SEEK_END	2

#define BUF_UNIT_SIZE	4096

class VS_MVO_CLASS CBufferIO  
{
public:
	CBufferIO(int64_t unitSize = BUF_UNIT_SIZE);
	virtual ~CBufferIO();

	//void SetUnitSize(int s) { m_unitSize = s; m_buff.SetBlockUnitSize(m_unitSize); }

	void Reset(){
	/*	SAFE_DEL_ARRAY(m_buff)

		m_buff = new unsigned char[BUF_UNIT_SIZE];
		memset(m_buff,0,sizeof(unsigned char)*BUF_UNIT_SIZE);
		m_size = BUF_UNIT_SIZE;
		m_bp = m_buff;
		m_length = 0;*/
		m_buff.RemoveAll();
		m_curIndex = 0;
	}

	BOOL IsEOF(){
		//return (m_bp == m_buff + m_length);
		return (m_curIndex == m_buff.GetCount());
	}

	void SetBuffer(const unsigned char* buffer, int64_t count);

	int64_t GetBufferCount(int64_t blockIndex = -1){
		if(blockIndex == -1)
			return m_buff.GetCount();
		else {
			int64_t nCount;
			m_buff.GetBlockData(blockIndex, nCount);
			return nCount;
		}
	}

	void GetBuffer(unsigned char* buffer, int64_t blockIndex = -1);

	void Seek(int64_t pos, int option = BUF_SEEK_SET){
		/*if(option == BUF_SEEK_SET)	m_bp = m_buff+pos;
		else if(option == BUF_SEEK_CUR)	m_bp += pos;
		else if(option == BUF_SEEK_END)	m_bp = m_buff + m_length + pos;*/

		if (option == BUF_SEEK_SET)	m_curIndex = pos;
		else if (option == BUF_SEEK_CUR)	m_curIndex += pos;
		else if (option == BUF_SEEK_END)	m_curIndex = m_buff.GetCount() + pos;
	}

	//int Tell() { return int(unsigned __int64(m_bp) - unsigned __int64(m_buff)); }
	int64_t Tell() { return m_curIndex; }

	void fread(void* buf, int64_t size, int64_t count, FILE* fp){  /// replace fread convendinetly
		Read((unsigned char*)buf, size*count);
	}
	
	void Read(unsigned char* bts, int64_t count)	{
		/*if (m_bp - m_buff + count > m_size) {
			assert(0);
			return;
		}

		memcpy(bts, m_bp, count);
		m_bp += count;*/

		//for (int i = 0; i < count; i++) bts[i] = m_buff[m_curIndex + i];
		//m_curIndex += count;

		if (count <= 0) return;

		if (m_curIndex + count > m_buff.GetCount()) {
			count = m_buff.GetCount() - m_curIndex;
		}

		m_buff.Fetch(bts, m_curIndex, count);
		m_curIndex += count;
	}

	void Read(unsigned char& cVal){
		Read(&cVal, 1);
	}
	
	void Read(int& iVal){
		Read((unsigned char*)&iVal, sizeof(int));
	}
	
	void Read(float& fVal){
		Read((unsigned char*)&fVal, sizeof(float));
	}

	void Read(double& dVal) {
		Read((unsigned char*)&dVal, sizeof(double));
	}
	
	void Read(long& lVal){
		Read((unsigned char*)&lVal, sizeof(long));
	}

	void Read(DWORD& dVal){
		Read((unsigned char*)&dVal, sizeof(DWORD));
	}

	void Read(unsigned int& uiVal) {
		Read((unsigned char*)&uiVal, sizeof(unsigned int));
	}

	void Read(DWORD_PTR& uiVal) {
		Read((unsigned char*)&uiVal, sizeof(DWORD_PTR));
	}

	void Read(char* string){
		int len = 0;
		Read(len);
		Read((unsigned char*)string, len);
	}
	
	void Read(float* fValArr, int64_t count){
		Read((unsigned char*)fValArr, sizeof(float)*count);
	}

	void Read(double* dValArr, int64_t count) {
		Read((unsigned char*)dValArr, sizeof(double)*count);
	}

	void Read(long* lValArr, int64_t count){
		Read((unsigned char*)lValArr, sizeof(long)*count);
	}

	void Read(int* iValArr, int64_t count) {
		Read((unsigned char*)iValArr, sizeof(int)*count);
	}

	void Read(unsigned int* uiValArr, int64_t count) {
		Read((unsigned char*)uiValArr, sizeof(unsigned int)*count);
	}

	void Read(DWORD* dValArr, int64_t count){
		Read((unsigned char*)dValArr, sizeof(DWORD)*count);
	}
	
	
//////////////////////////////////////////////////////////////////////////

	void fwrite(const void* buf, int64_t size, int64_t count, FILE* fp){  /// replace fwrite convendinetly
		Write((unsigned char*)buf, size*count);
	}

	void Write(const unsigned char* bts, int64_t count){
		Append(count, bts);
	}

	void Write(unsigned char cVal){
		Write(&cVal, 1);
	}
	
	void Write(int iVal){
		Write((unsigned char*)&iVal, sizeof(int));
	}

	
	void Write(float fVal){
		Write((unsigned char*)&fVal, sizeof(float));
	}

	void Write(double dVal) {
		Write((unsigned char*)&dVal, sizeof(double));
	}
	
	void Write(long lVal){
		Write((unsigned char*)&lVal, sizeof(long));
	}

	void Write(DWORD dVal){
		Write((unsigned char*)&dVal, sizeof(DWORD));
	}

	void Write(unsigned int uiVal) {
		Write((unsigned char*)&uiVal, sizeof(unsigned int));
	}

	void Write(DWORD_PTR dpVal) {
		Write((unsigned char*)&dpVal, sizeof(DWORD_PTR));
	}

	void Write(const char* string){
		if(string == NULL){
			Write(int(1));
			Write((unsigned char*)(""), 1);
		}
		else{
			Write(int(strlen(string)+1));
			Write((unsigned char*)string, strlen(string)+1);
		}
	}
	
	void Write(const float* fValArr, int64_t count){
		Write((const unsigned char*)fValArr, sizeof(float)*count);
	}

	void Write(const double* dValArr, int64_t count) {
		Write((const unsigned char*)dValArr, sizeof(double)*count);
	}

	void Write(const long* lValArr, int64_t count){
		Write((const unsigned char*)lValArr, sizeof(long)*count);
	}

	void Write(const int* iValArr, int64_t count) {
		Write((const unsigned char*)iValArr, sizeof(int)*count);
	}

	void Write(const unsigned int* uiValArr, int64_t count) {
		Write((const unsigned char*)uiValArr, sizeof(unsigned int)*count);
	}

	void Write(const DWORD* dValArr, int64_t count){
		Write((const unsigned char*)dValArr, sizeof(DWORD)*count);
	}

	BOOL WriteToFile(FILE* fp);
	BOOL ReadFromFile(FILE* fp);

	BOOL WriteToFile32(FILE* fp);
	BOOL ReadFromFile32(FILE* fp);

	BOOL WriteToFile64(FILE* fp);
	BOOL ReadFromFile64(FILE* fp);

	void SetSizeAs64(BOOL b) { m_bSize64 = b; }
	BOOL IsSize64() { return m_bSize64; }
	
protected:
	

	void Append(int64_t count, const unsigned char* bts){

	/*	if( (m_bp-m_buff) + count > m_size ){
			m_size = ((int(unsigned __int64(m_bp) - unsigned __int64(m_buff))+count)/ m_unitSize +1)*m_unitSize;
			unsigned char* buf = new unsigned char[ m_size ];
			memcpy(buf, m_buff, int(unsigned __int64(m_bp) - unsigned __int64(m_buff)));
			m_bp = buf + int(unsigned __int64(m_bp) - unsigned __int64(m_buff));
			
			SAFE_DEL_ARRAY(m_buff)
			m_buff = buf;
		}
		memcpy(m_bp, bts, count);
		m_bp += count;
		m_length += count;*/

		m_buff.Append(bts, count);
		m_curIndex = m_buff.GetCount();
	}
	
protected:
	//unsigned char* m_buff;
	//unsigned char* m_bp;
	ByteArray m_buff;
	int64_t m_curIndex;

	//int m_size;
	//int m_length;
	
	int64_t m_unitSize;

	BOOL m_bSize64;
};

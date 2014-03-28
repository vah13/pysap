/*
## ===========
## pysapcompress - SAP Compression library wrapper for Python
##
## Copyright (C) 2014 Core Security Technologies
##
## The library was designed and developed by Martin Gallo of Core Security
## Technologies - Security Consulting Services
##
## Based on the work performed by Dennis Yurichev <dennis@conus.info>
##
## This program is free software; you can redistribute it and/or
## modify it under the terms of the GNU General Public License
## as published by the Free Software Foundation; either version 2
## of the License, or (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##==============
*/

#include <Python.h>

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "hpa101saptype.h"
#include "hpa104CsObject.h"
#include "hpa106cslzc.h"
#include "hpa107cslzh.h"
#include "hpa105CsObjInt.h"


/* Memory allocation factor constant */
/* XXX: Fix this, find a way to allocate the appropriate size according to the input */
#define MEMORY_ALLOC_FACTOR 10


/* Compression algorithm constants */
#define ALG_LZC CS_LZC
#define ALG_LZH CS_LZH


/* Return code for memory errors */
#define CS_E_MEMORY_ERROR -99


/* Returns an error strings for compression library return codes */
const static char *error_string(int return_code){
	switch (return_code){
	case CS_IEND_OF_STREAM: return "end of data (internal)";
	case CS_IEND_OUTBUFFER: return "end of output buffer";
	case CS_IEND_INBUFFER: return "end of input buffer";
	case CS_E_OUT_BUFFER_LEN: return "invalid output length";
	case CS_E_IN_BUFFER_LEN: return "invalid input length";
	case CS_E_NOSAVINGS: return "no savings";
	case CS_E_INVALID_SUMLEN: return "invalid len of stream";
	case CS_E_IN_EQU_OUT: return "inbuf == outbuf";
	case CS_E_INVALID_ADDR: return "inbuf == NULL,outbuf == NULL";
	case CS_E_FATAL: return "internal error !";
	case CS_E_BOTH_ZERO: return "inlen = outlen = 0";
	case CS_E_UNKNOWN_ALG: return "unknown algorithm";
	case CS_E_UNKNOWN_TYPE: return "";
	/* for decompress */
	case CS_E_FILENOTCOMPRESSED: return "input not compressed";
	case CS_E_MAXBITS_TOO_BIG: return "maxbits to large";
	case CS_E_BAD_HUF_TREE: return "bad hufman tree";
	case CS_E_NO_STACKMEM: return "no stack memory in decomp";
	case CS_E_INVALIDCODE: return "invalid code";
	case CS_E_BADLENGTH: return "bad lengths";
	case CS_E_STACK_OVERFLOW: return "stack overflow in decomp";
	case CS_E_STACK_UNDERFLOW: return "stack underflow in decomp";
	/* only Windows */
	case CS_NOT_INITIALIZED: return "storage not allocated";
	/* non error return codes */
	case CS_END_INBUFFER: return "end of input buffer";
	case CS_END_OUTBUFFER: return "end of output buffer";
	case CS_END_OF_STREAM: return "end of data";
	/* unknown error */
	default: return "unknown error";
	}
}


/* Custom exception for compression library errors */
static char compression_exception_short[] = "CompressError";
static char compression_exception_name[] = "pysapcompress.CompressError";
PyObject *compression_exception = NULL;

static char decompression_exception_short[] = "DecompressError";
static char decompression_exception_name[] = "pysapcompress.DecompressError";
PyObject *decompression_exception = NULL;


/* Hexdump helper function for debugging */
void hexdump(unsigned char *address, unsigned int size)
{
	unsigned int i = 0, j = 0, offset = 0;
	printf("[%08x] ", offset);
	for (; i<size; i++){
		j++; printf("%02x ", address[i]);
		if (j==8) printf(" ");
		if (j==16){
			offset+=j; printf("\n[%08x] ", offset); j=0;
		}
	}
	printf("\n");
}


/* Decompress a packet buffer */
int decompress_packet (const unsigned char *in, const int in_length, unsigned char **out, int *out_length)
{
	class CsObjectInt o;
	SAP_INT bufout_length = 0, bytes_read = 0, bytes_decompressed = 0, bufin_length = 0;
	int rt = 0;
	SAP_BYTE *bufin = NULL, *bufout = NULL;

#ifdef DEBUG
	printf("pysapcompress.cpp: Decompressing (%d bytes, reported length of %d bytes)...\n", in_length, *out_length);
#endif

	*out = NULL;

	/* Check for invalid inputs */
	if (in == NULL)
		return CS_E_INVALID_ADDR;
	if (in_length <= 0)
		return CS_E_IN_BUFFER_LEN;
	if (*out_length <= 0)
		return CS_E_OUT_BUFFER_LEN;

	/* Allocate the input buffer and copy the input */
	bufin = (SAP_BYTE*) malloc(in_length);
	if (!bufin){
		return CS_E_MEMORY_ERROR;
	}
	for (int i = 0; i < in_length; i++)
		bufin[i] = (SAP_BYTE) in[i];

	bufin_length = (SAP_INT)in_length;

	/* Allocate the output buffer. We use the reported output size
	 * as the output buffer size.
	 */
	bufout_length = *out_length;
	bufout = (SAP_BYTE*) malloc(bufout_length);
	if (!bufout){
		free(bufin);
		return CS_E_MEMORY_ERROR;
	}
	memset(bufout, 0, bufout_length);

#ifdef DEBUG_TRACE
	printf("pysapcompress.cpp: Input buffer %p (%d bytes), output buffer %p (%d bytes)\n", bufin, bufin_length, bufout, bufout_length);
#endif

	rt=o.CsDecompr(bufin, bufin_length, bufout, bufout_length, CS_INIT_DECOMPRESS, &bytes_read, &bytes_decompressed);

#ifdef DEBUG
	printf("pysapcompress.cpp: Return code %d (%s) (%d bytes read, %d bytes decompressed)\n", rt, error_string(rt), bytes_read, bytes_decompressed);
#endif

	/* Successful decompression */
	if (rt == CS_END_OF_STREAM || rt == CS_END_INBUFFER || rt == CS_END_OUTBUFFER) {
		*out_length = bytes_decompressed;

        /* Allocate the required memory */
        *out = (unsigned char *)malloc(bytes_decompressed);
        if (*out){

		    /* Copy the buffer in the out parameter */
		    for (int i = 0; i < bytes_decompressed; i++)
			    (*out)[i] = (char) bufout[i];

#ifdef DEBUG_TRACE
		    printf("pysapcompress.cpp: Out buffer:\n");
			hexdump(*out, bytes_decompressed);
#endif
		/* Memory error */
        } else {
    		rt = CS_E_MEMORY_ERROR; *out_length = 0;
        }
	}

	/* Free the buffers */
	free (bufin); free (bufout);

#ifdef DEBUG
	printf("pysapcompress.cpp: Out Length: %d\n", *out_length);
#endif

	return rt;
};


/* Compress a packet buffer */
int compress_packet (const unsigned char *in, const int in_length, unsigned char **out, int *out_length, const unsigned int algorithm)
{
	class CsObjectInt o;
	SAP_INT bufout_length = 0, bytes_read = 0, bytes_compressed = 0, bufin_length = 0;
	int rt = 0;
	SAP_BYTE *bufin = NULL, *bufout = NULL;

#ifdef DEBUG
	printf("pysapcompress.cpp: Compressing (%d bytes) using algorithm %s ...\n", in_length, algorithm==ALG_LZC?"LZC":algorithm==ALG_LZH?"LZH":"unknown");
#endif

	*out = NULL; *out_length = 0;

	/* Check for invalid inputs */
	if (in == NULL)
		return CS_E_INVALID_ADDR;
	if (in_length <= 0)
		return CS_E_IN_BUFFER_LEN;

	/* Allocate the input buffer and copy the input */
	bufin_length = (SAP_INT)in_length;
	bufin = (SAP_BYTE*) malloc(bufin_length);
	if (!bufin){
		return CS_E_MEMORY_ERROR;
	}
	memset(bufin, 0, bufin_length);

	for (int i = 0; i < in_length; i++)
		bufin[i] = (SAP_BYTE) in[i];

	/* Allocate the output buffer. We use the input size with a constant factor
	 * as the output buffer size.
	 */
	bufout_length = bufin_length * MEMORY_ALLOC_FACTOR;
	bufout = (SAP_BYTE*) malloc(bufout_length);
	if (!bufout){
		free(bufin);
		return CS_E_MEMORY_ERROR;
	}
	memset(bufout, 0, bufout_length);

#ifdef DEBUG_TRACE
	printf("pysapcompress.cpp: Input buffer %p (%d bytes), output buffer %p (%d bytes)\n", bufin, bufin_length, bufout, bufout_length);
#endif

	rt = o.CsCompr(bufin_length, bufin, bufin_length, bufout, bufout_length, CS_INIT_COMPRESS | algorithm, &bytes_read, &bytes_compressed);

#ifdef DEBUG
	printf("pysapcompress.cpp: Return code %d (%s) (%d bytes read, %d bytes compressed)\n", rt, error_string(rt), bytes_read, bytes_compressed);
#endif

	/* Successful compression */
	if (rt == CS_END_OF_STREAM || rt == CS_END_INBUFFER || rt == CS_END_OUTBUFFER) {
		*out_length = bytes_compressed;

        /* Allocate the required memory */
        *out = (unsigned char *)malloc(bytes_compressed);
        if (*out){

		    /* Copy the buffer in the out parameter */
		    memcpy(*out, bufout, bytes_compressed);

#ifdef DEBUG_TRACE
		    printf("pysapcompress.cpp: Out buffer:\n");
			hexdump(*out, bytes_compressed);
#endif
		/* Memory error */
        } else {
        	rt = CS_E_MEMORY_ERROR; *out_length = 0;
        }
	}

	/* Free the buffers */
	free (bufin); free (bufout);

#ifdef DEBUG
	printf("pysapcompress.cpp: Out Length: %d\n", *out_length);
#endif

	return rt;
};


/* Compress Python function */
static PyObject *
pysapcompress_compress(PyObject *self, PyObject *args, PyObject *keywds)
{
    const unsigned char *in = NULL;
    unsigned char *out = NULL;
    int status = 0, in_length = 0, out_length = 0, algorithm = ALG_LZC;

    /* Define the keyword list */
    static char kwin[] = "in";
    static char kwalgorithm[] = "algorithm";
    static char* kwlist[] = {kwin, kwalgorithm, NULL};

    /* Parse the parameters. We are also interested in the length of the input buffer. */
    if (!PyArg_ParseTupleAndKeywords(args, keywds, "s#|i", kwlist, &in, &in_length, &algorithm))
        return NULL;

    /* Call the compression function */
    status = compress_packet(in, in_length, &out, &out_length, algorithm);

    /* Perform some exception handling */
    if (status < 0){
    	/* If it was a memory error in this module, raise a NoMemory standard exception */
    	if (status==CS_E_MEMORY_ERROR)
    		return PyErr_NoMemory();
    	/* If the error was in the compression module, raise a custom exception */
    	else {
    		return PyErr_Format(compression_exception, "Compression error (%s)", error_string(status));
    	}
    }

    /* It no error was raised, return the compressed buffer and the length */
	return Py_BuildValue("iis#", status, out_length, out, out_length);
}


/* Decompress Python function */
static PyObject *
pysapcompress_decompress(PyObject *self, PyObject *args)
{
    const unsigned char *in = NULL;
    unsigned char *out = NULL;
    int status = 0, in_length = 0, out_length = 0;

    /* Parse the parameters. We are also interested in the length of the input buffer. */
    if (!PyArg_ParseTuple(args, "s#i", &in, &in_length, &out_length))
        return NULL;

    /* Call the compression function */
    status = decompress_packet(in, in_length, &out, &out_length);

    /* Perform some exception handling */
    if (status < 0){
    	/* If it was a memory error in this module, raise a NoMemory standard exception */
    	if (status==CS_E_MEMORY_ERROR)
    		return PyErr_NoMemory();
    	/* If the error was in the compression module, raise a custom exception */
    	else
    		return PyErr_Format(decompression_exception, "Decompression error (%s)", error_string(status));
    }
    /* It no error was raised, return the uncompressed buffer and the length */
    return Py_BuildValue("iis#", status, out_length, out, out_length);
}


/* Method definitions */
static PyMethodDef pysapcompressMethods[] = {
    {"compress", (PyCFunction)pysapcompress_compress, METH_VARARGS | METH_KEYWORDS, "Compress a buffer using the SAP Compression functions."},
    {"decompress", pysapcompress_decompress, METH_VARARGS, "Decompress a buffer using the SAP Compression functions."},
    {NULL, NULL, 0, NULL}
};


/* Module initialization */
PyMODINIT_FUNC
initpysapcompress(void)
{
    PyObject *module = NULL;
    /* Create the module and define the methods */
    module = Py_InitModule("pysapcompress", pysapcompressMethods);

    /* Add the algorithm constants */
    PyModule_AddIntConstant (module, "ALG_LZC", ALG_LZC);
    PyModule_AddIntConstant (module, "ALG_LZH", ALG_LZH);

    /* Create a custom exception and add it to the module */
    compression_exception = PyErr_NewException(compression_exception_name, NULL, NULL);
    PyModule_AddObject(module, compression_exception_short, compression_exception);

    decompression_exception = PyErr_NewException(decompression_exception_name, NULL, NULL);
    PyModule_AddObject(module, decompression_exception_short, decompression_exception);

}

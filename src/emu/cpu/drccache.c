/***************************************************************************

    drccache.c

    Universal dynamic recompiler cache management.

****************************************************************************

    Copyright Aaron Giles
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are
    met:

        * Redistributions of source code must retain the above copyright
          notice, this list of conditions and the following disclaimer.
        * Redistributions in binary form must reproduce the above copyright
          notice, this list of conditions and the following disclaimer in
          the documentation and/or other materials provided with the
          distribution.
        * Neither the name 'MAME' nor the names of its contributors may be
          used to endorse or promote products derived from this software
          without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY AARON GILES ''AS IS'' AND ANY EXPRESS OR
    IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
    WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    DISCLAIMED. IN NO EVENT SHALL AARON GILES BE LIABLE FOR ANY DIRECT,
    INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
    (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
    SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
    HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
    STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
    IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.

***************************************************************************/

#include "emu.h"
#include "drccache.h"



//**************************************************************************
//  MACROS
//**************************************************************************

// ensure that all memory allocated is aligned to an 8-byte boundary
#define ALIGN_PTR_UP(p)			((void *)(((FPTR)(p) + (CACHE_ALIGNMENT - 1)) & ~(CACHE_ALIGNMENT - 1)))
#define ALIGN_PTR_DOWN(p)		((void *)((FPTR)(p) & ~(CACHE_ALIGNMENT - 1)))



//**************************************************************************
//  DRC CACHE
//**************************************************************************

//-------------------------------------------------
//  drc_cache - constructor
//-------------------------------------------------

drc_cache::drc_cache(size_t bytes)
	: m_near((drccodeptr)osd_alloc_executable(bytes)),
	  m_neartop(m_near),
	  m_base(m_near + NEAR_CACHE_SIZE),
	  m_top(m_base),
	  m_end(m_near + bytes),
	  m_codegen(0),
	  m_size(bytes)
{
	memset(m_free, 0, sizeof(m_free));
	memset(m_nearfree, 0, sizeof(m_nearfree));
}


//-------------------------------------------------
//  ~drc_cache - destructor
//-------------------------------------------------

drc_cache::~drc_cache()
{
	// release the memory
	osd_free_executable(m_near, m_size);
}



//-------------------------------------------------
//  flush - flush the cache contents
//-------------------------------------------------

void drc_cache::flush()
{
	// can't flush in the middle of codegen
	assert(m_codegen == NULL);

	// just reset the top back to the base and re-seed
	m_top = m_base;
}


//-------------------------------------------------
//  alloc - allocate permanent memory from the
//  cache
//-------------------------------------------------

void *drc_cache::alloc(size_t bytes)
{
	assert(bytes > 0);

	// pick first from the free list
	if (bytes < MAX_PERMANENT_ALLOC)
	{
		free_link **linkptr = &m_free[(bytes + CACHE_ALIGNMENT - 1) / CACHE_ALIGNMENT];
		free_link *link = *linkptr;
		if (link != NULL)
		{
			*linkptr = link->m_next;
			return link;
		}
	}

	// if no space, we just fail
	drccodeptr ptr = (drccodeptr)ALIGN_PTR_DOWN(m_end - bytes);
	if (m_top > ptr)
		return NULL;

	// otherwise update the end of the cache
	m_end = ptr;
	return ptr;
}


//-------------------------------------------------
//  alloc_near - allocate permanent memory from
//  the near part of the cache
//-------------------------------------------------

void *drc_cache::alloc_near(size_t bytes)
{
	assert(bytes > 0);

	// pick first from the free list
	if (bytes < MAX_PERMANENT_ALLOC)
	{
		free_link **linkptr = &m_nearfree[(bytes + CACHE_ALIGNMENT - 1) / CACHE_ALIGNMENT];
		free_link *link = *linkptr;
		if (link != NULL)
		{
			*linkptr = link->m_next;
			return link;
		}
	}

	// if no space, we just fail
	drccodeptr ptr = (drccodeptr)ALIGN_PTR_UP(m_neartop);
	if (ptr + bytes > m_base)
		return NULL;

	// otherwise update the top of the near part of the cache
	m_neartop = ptr + bytes;
	return ptr;
}


//-------------------------------------------------
//  alloc_temporary - allocate temporary memory
//  from the cache
//-------------------------------------------------

void *drc_cache::alloc_temporary(size_t bytes)
{
	// can't allocate in the middle of codegen
	assert(m_codegen == NULL);

	// if no space, we just fail
	drccodeptr ptr = m_top;
	if (ptr + bytes >= m_end)
		return NULL;

	// otherwise, update the cache top
	m_top = (drccodeptr)ALIGN_PTR_UP(ptr + bytes);
	return ptr;
}


//-------------------------------------------------
//  free - release permanent memory allocated from
//  the cache
//-------------------------------------------------

void drc_cache::dealloc(void *memory, size_t bytes)
{
	assert(bytes < MAX_PERMANENT_ALLOC);
	assert(((drccodeptr)memory >= m_near && (drccodeptr)memory < m_base) || ((drccodeptr)memory >= m_end && (drccodeptr)memory < m_near + m_size));

	// determine which free list to add to
	free_link **linkptr;
	if ((drccodeptr)memory < m_base)
		linkptr = &m_nearfree[(bytes + CACHE_ALIGNMENT - 1) / CACHE_ALIGNMENT];
	else
		linkptr = &m_free[(bytes + CACHE_ALIGNMENT - 1) / CACHE_ALIGNMENT];

	// link is into the free list for our size
	free_link *link = (free_link *)memory;
	link->m_next = *linkptr;
	*linkptr = link;
}


//-------------------------------------------------
//  begin_codegen - begin code generation
//-------------------------------------------------

drccodeptr *drc_cache::begin_codegen(UINT32 reserve_bytes)
{
	// can't restart in the middle of codegen
	assert(m_codegen == NULL);
	assert(m_ooblist.first() == NULL);

	// if still no space, we just fail
	drccodeptr ptr = m_top;
	if (ptr + reserve_bytes >= m_end)
		return NULL;

	// otherwise, return a pointer to the cache top
	m_codegen = m_top;
	return &m_top;
}


//-------------------------------------------------
//  end_codegen - complete code generation
//-------------------------------------------------

drccodeptr drc_cache::end_codegen()
{
	drccodeptr result = m_codegen;

	// run the OOB handlers
	oob_handler *oob;
	while ((oob = m_ooblist.detach_head()) != NULL)
	{
		// call the callback
		(*oob->m_callback)(&m_top, oob->m_param1, oob->m_param2, oob->m_param3);
		assert(m_top - m_codegen < CODEGEN_MAX_BYTES);

		// release our memory
		dealloc(oob, sizeof(*oob));
	}

	// update the cache top
	m_top = (drccodeptr)ALIGN_PTR_UP(m_top);
	m_codegen = NULL;

	return result;
}


//-------------------------------------------------
//  request_oob_codegen - request callback for
//  out-of-band codegen
//-------------------------------------------------

void drc_cache::request_oob_codegen(oob_func callback, void *param1, void *param2, void *param3)
{
	assert(m_codegen != NULL);

	// pull an item from the free list
	oob_handler *oob = (oob_handler *)alloc(sizeof(*oob));
	assert(oob != NULL);

	// fill it in
	oob->m_callback = callback;
	oob->m_param1 = param1;
	oob->m_param2 = param2;
	oob->m_param3 = param3;

	// add to the tail
	m_ooblist.append(*oob);
}

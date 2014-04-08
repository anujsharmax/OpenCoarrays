/* Single-Image implementation of Libcaf

Copyright (c) 2012-2014, OpenCoarray Consortium
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the <organization> nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.  */

#include "libcaf.h"
#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>	/* For memcpy.  */
#include <stdarg.h>	/* For variadic arguments.  */
#include <alloca.h>


/* Define GFC_CAF_CHECK to enable run-time checking.  */
/* #define GFC_CAF_CHECK  1  */

typedef MPI_Win *mpi_caf_token_t;
#define TOKEN(X) ((mpi_caf_token_t) (X))

static void error_stop (int error) __attribute__ ((noreturn));

/* Global variables.  */
static int caf_this_image;
static int caf_num_images;
static int caf_is_finalized;

/*Sync image part*/
/*By default the two array have initially 64 items*/

caf_static_t *caf_static_list = NULL;
caf_static_t *caf_tot = NULL;

/* Keep in sync with single.c.  */
static void
caf_runtime_error (const char *message, ...)
{
  va_list ap;
  fprintf (stderr, "Fortran runtime error on image %d: ", caf_this_image);
  va_start (ap, message);
  vfprintf (stderr, message, ap);
  va_end (ap);
  fprintf (stderr, "\n");

  /* FIXME: Shutdown the Fortran RTL to flush the buffer.  PR 43849.  */
  /* FIXME: Do some more effort than just to abort.  */
  MPI_Finalize();

  /* Should be unreachable, but to make sure also call exit.  */
  exit (EXIT_FAILURE);
}


/* Initialize coarray program.  This routine assumes that no other
   GASNet initialization happened before. */

void
PREFIX(init) (int *argc, char ***argv)
{
  if (caf_num_images == 0)
    {
      int ierr = 0;

      MPI_Init(argc,argv);

      if (unlikely ((ierr != MPI_SUCCESS)))
	caf_runtime_error ("Failure when initializing MPI: %d", ierr);

      MPI_Comm_size(MPI_COMM_WORLD,&caf_num_images);
      MPI_Comm_rank(MPI_COMM_WORLD,&caf_this_image);

      caf_this_image++;
      caf_is_finalized = 0;

    }
}


/* Finalize coarray program.   */

void
PREFIX(finalize) (void)
{

  MPI_Barrier(MPI_COMM_WORLD);

  while (caf_static_list != NULL)
    {
      caf_static_t *tmp = caf_static_list->prev;

      free (caf_static_list);
      caf_static_list = tmp;
    }

  MPI_Finalize();

  caf_is_finalized = 1;
}


int
PREFIX(this_image)(int distance __attribute__ ((unused)))
{
  return caf_this_image;
}


int
PREFIX(num_images)(int distance __attribute__ ((unused)),
                         int failed __attribute__ ((unused)))
{
  return caf_num_images;
}


void *
PREFIX(register) (size_t size, caf_register_t type, caf_token_t *token,
		  int *stat, char *errmsg, int errmsg_len)
{
  /* int ierr; */
  void *mem;

  if (unlikely (caf_is_finalized))
    goto error;

  /* Start GASNET if not already started.  */
  if (caf_num_images == 0)
    PREFIX(init) (NULL, NULL);

  /* Token contains only a list of pointers.  */

  *token = malloc(sizeof(MPI_Win));

  MPI_Alloc_mem(size,MPI_INFO_NULL,&mem);

  MPI_Win_create(mem,size,1,MPI_INFO_NULL,MPI_COMM_WORLD,*token);

  MPI_Win *p = *token;

  MPI_Win_fence(0, *p);

  caf_static_t *tmp = malloc (sizeof (caf_static_t));
  tmp->prev  = caf_tot;
  tmp->token = *token;
  caf_tot = tmp;

  if (type == CAF_REGTYPE_COARRAY_STATIC)
    {
      tmp = malloc (sizeof (caf_static_t));
      tmp->prev  = caf_static_list;
      tmp->token = *token;
      caf_static_list = tmp;
    }

  if (stat)
    *stat = 0;

  return mem;

error:
  {
    char *msg;

    if (caf_is_finalized)
      msg = "Failed to allocate coarray - there are stopped images";
    else
      msg = "Failed to allocate coarray";

    if (stat)
      {
	*stat = caf_is_finalized ? STAT_STOPPED_IMAGE : 1;
	if (errmsg_len > 0)
	  {
	    int len = ((int) strlen (msg) > errmsg_len) ? errmsg_len
							: (int) strlen (msg);
	    memcpy (errmsg, msg, len);
	    if (errmsg_len > len)
	      memset (&errmsg[len], ' ', errmsg_len-len);
	  }
      }
    else
      caf_runtime_error (msg);
  }

  return NULL;
}


void
PREFIX(deregister) (caf_token_t *token, int *stat, char *errmsg, int errmsg_len)
{
  /* int ierr; */

  if (unlikely (caf_is_finalized))
    {
      const char msg[] = "Failed to deallocate coarray - "
			  "there are stopped images";
      if (stat)
	{
	  *stat = STAT_STOPPED_IMAGE;
	
	  if (errmsg_len > 0)
	    {
	      int len = ((int) sizeof (msg) - 1 > errmsg_len)
			? errmsg_len : (int) sizeof (msg) - 1;
	      memcpy (errmsg, msg, len);
	      if (errmsg_len > len)
		memset (&errmsg[len], ' ', errmsg_len-len);
	    }
	  return;
	}
      caf_runtime_error (msg);
    }

  PREFIX(sync_all) (NULL, NULL, 0);

  if (stat)
    *stat = 0;

  /* if (unlikely (ierr = ARMCI_Free ((*token)[caf_this_image-1]))) */
  /*   caf_runtime_error ("ARMCI memory freeing failed: Error code %d", ierr); */
  //gasnet_exit(0);
  
  free (*token);
}


void
PREFIX(sync_all) (int *stat, char *errmsg, int errmsg_len)
{
  int ierr;

  if (unlikely (caf_is_finalized))
    ierr = STAT_STOPPED_IMAGE;
  else
    {
      caf_static_t *tmp = caf_tot, *next = caf_tot;

      MPI_Win *p = tmp->token;

      MPI_Win_fence(0,*p);

      while(tmp && tmp->prev != NULL)
	{
	  next = tmp;
	  tmp=tmp->prev;
	  p = next->token;
	  MPI_Win_fence(0,*p);
	}

      MPI_Barrier(MPI_COMM_WORLD);
      ierr = 0;
    }
 
  if (stat)
    *stat = ierr;

  if (ierr)
    {
      char *msg;
      if (caf_is_finalized)
	msg = "SYNC ALL failed - there are stopped images";
      else
	msg = "SYNC ALL failed";

      if (errmsg_len > 0)
	{
	  int len = ((int) strlen (msg) > errmsg_len) ? errmsg_len
						      : (int) strlen (msg);
	  memcpy (errmsg, msg, len);
	  if (errmsg_len > len)
	    memset (&errmsg[len], ' ', errmsg_len-len);
	}
      else
	caf_runtime_error (msg);
    }
}

/* token: The token of the array to be written to. */
/* offset: Difference between the coarray base address and the actual data, used for caf(3)[2] = 8 or caf[4]%a(4)%b = 7. */
/* image_index: Index of the coarray (typically remote, though it can also be on this_image). */
/* data: Pointer to the to-be-transferred data. */
/* size: The number of bytes to be transferred. */
/* asynchronous: Return before the data transfer has been complete  */

void
PREFIX(send) (caf_token_t token, size_t offset, int image_index, void *data,
	      size_t size, bool async)
{
  int ierr = 0;

  MPI_Win *p = token;

  if(async==false)
    ierr = MPI_Put(data,size,MPI_BYTE,image_index-1,offset,size,MPI_BYTE,*p);
   
    //gasnet_put_bulk(image_index-1, tm[image_index-1]+offset, data, size);
  /* else */
  /*   ierr = ARMCI_NbPut(data,t.addr+offset,size,image_index-1,NULL); */
  if(ierr != 0)
    error_stop (ierr);
}

/* void */
/* PREFIX(get) (caf_token_t token, size_t offset, int image_index, void *data, size_t size, bool async) */
/* { */
/*   int ierr = 0; */

/*   void **tm = token; */

/*   if(async==false) */
/*     gasnet_get_bulk(data,image_index-1,tm[image_index-1]+offset,size); */
/*   /\* else *\/ */
/*   /\*   ierr = ARMCI_NbPut(data,t.addr+offset,size,image_index-1,NULL); *\/ */
/*   if(ierr != 0) */
/*     error_stop (ierr); */

/* } */


/* SYNC IMAGES. Note: SYNC IMAGES(*) is passed as count == -1 while
   SYNC IMAGES([]) has count == 0. Note further that SYNC IMAGES(*)
   is not equivalent to SYNC ALL. */
/* void */
/* PREFIX(sync_images) (int count, int images[], int *stat, char *errmsg, */
/* 		     int errmsg_len) */
/* { */
/*   int ierr,i=0; */
/*   if (count == 0 || (count == 1 && images[0] == caf_this_image)) */
/*     { */
/*       if (stat) */
/* 	*stat = 0; */
/*       return; */
/*     } */

/* #ifdef GFC_CAF_CHECK */
/*   { */
/*     for (i = 0; i < count; i++) */
/*       if (images[i] < 1 || images[i] > caf_num_images) */
/* 	{ */
/* 	  fprintf (stderr, "COARRAY ERROR: Invalid image index %d to SYNC " */
/* 		   "IMAGES", images[i]); */
/* 	  error_stop (1); */
/* 	} */
/*   } */
/* #endif */

/*   if (unlikely (caf_is_finalized)) */
/*     ierr = STAT_STOPPED_IMAGE; */
/*   else */
/*     { */
/*       if(count == -1) */
/* 	insOrders(images_full,caf_num_images-1); */
/*       else */
/* 	insOrders(images, count); */

/*       GASNET_BLOCKUNTIL(freeToGo() == true); */

/*       ierr = 0; */
/*     } */

/*   if (stat) */
/*     *stat = ierr; */

/*   if (ierr) */
/*     { */
/*       char *msg; */
/*       if (caf_is_finalized) */
/* 	msg = "SYNC IMAGES failed - there are stopped images"; */
/*       else */
/* 	msg = "SYNC IMAGES failed"; */

/*       if (errmsg_len > 0) */
/* 	{ */
/* 	  int len = ((int) strlen (msg) > errmsg_len) ? errmsg_len */
/* 						      : (int) strlen (msg); */
/* 	  memcpy (errmsg, msg, len); */
/* 	  if (errmsg_len > len) */
/* 	    memset (&errmsg[len], ' ', errmsg_len-len); */
/* 	} */
/*       else */
/* 	caf_runtime_error (msg); */
/*     } */
/* } */


/* ERROR STOP the other images.  */

static void
error_stop (int error)
{
  /* FIXME: Shutdown the Fortran RTL to flush the buffer.  PR 43849.  */
  /* FIXME: Do some more effort than just gasnet_exit().  */
  MPI_Finalize();

  /* Should be unreachable, but to make sure also call exit.  */
  exit (error);
}


/* ERROR STOP function for string arguments.  */

void
PREFIX(error_stop_str) (const char *string, int32_t len)
{
  fputs ("ERROR STOP ", stderr);
  while (len--)
    fputc (*(string++), stderr);
  fputs ("\n", stderr);

  error_stop (1);
}


/* ERROR STOP function for numerical arguments.  */

void
PREFIX(error_stop) (int32_t error)
{
  fprintf (stderr, "ERROR STOP %d\n", error);
  error_stop (error);
}
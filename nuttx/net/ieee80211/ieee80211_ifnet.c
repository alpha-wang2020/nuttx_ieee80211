/****************************************************************************
 * net/ieee80211/ieee80211_ifnet.c
 *
 *   Copyright (C) 2014 Gregory Nutt. All rights reserved.
 *   Author: Gregory Nutt <gnutt@nuttx.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name NuttX nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <string.h>
#include <queue.h>

#include <nuttx/net/ieee80211/ieee80211_ifnet.h>
#include <nuttx/net/ieee80211/ieee80211_var.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Types
 ****************************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* This is a pool of pre-allocated I/O buffers */

static struct ieee80211_iobuf_s g_iopool[CONFIG_IEEE80211_NBUFFERS];
static bool g_ioinitialized;

/****************************************************************************
 * Public Data
 ****************************************************************************/

/* A list of all free, unallocated I/O buffers */

sq_queue_t g_ieee80211_freelist;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: ieee80211_ifinit
 *
 * Description:
 *   Set up the devices interface I/O buffers for normal operations.
 *
 ****************************************************************************/

void ieee80211_ifinit(FAR struct ieee80211com *ic)
{
  int i;

  /* Perform one-time initialization */

  if (!g_ioinitialized)
    {
      /* Add each I/O buffer to the free list */

      for (i = 0; i < CONFIG_IEEE80211_NBUFFERS; i++)
        {
          sq_addlast(&g_iopool[i].m_link, &g_ieee80211_freelist);
        }

      g_ioinitialized = true;
    }

  /* Perform pre-instance initialization */
  /* NONE */
}

/****************************************************************************
 * Name: ieee80211_iofree
 *
 * Description:
 *   Free the I/O buffer at the head of a buffer chain returning it to the
 *   free list.  The link to  the next I/O buffer in the chain is return.
 *
 ****************************************************************************/

FAR struct ieee80211_iobuf_s *ieee80211_iofree(FAR struct ieee80211_iobuf_s *iob)
{
  sq_entry_t *next = iob->m_link.flink;

  sq_addlast(&iob->m_link, &g_ieee80211_freelist);
  return (FAR struct ieee80211_iobuf_s *)next;
}

/****************************************************************************
 * Name: ieee80211_iopurge
 *
 * Description:
 *   Free an entire buffer chain
 *
 ****************************************************************************/

void ieee80211_iopurge(FAR sq_queue_t *q)
{
  /* If the free list is empty, then just move the entry queue to the the
   * free list.  Otherwise, append the list to the end of the free list.
   */

  if (g_ieee80211_freelist.tail)
    {
      g_ieee80211_freelist.tail->flink = q->head;
    }
  else
    {
      g_ieee80211_freelist.head = q->head;
    }

  /* In either case, the tail of the queue is the tail of queue becomes the
   * tail of the free list.
   */

  g_ieee80211_freelist.tail = q->tail;
}

/****************************************************************************
 * Name: ieee80211_iocat
 *
 * Description:
 *   Concatenate ieee80211_iobuf_s chain iob2 to iob1.
 *
 ****************************************************************************/

void ieee80211_iocat(FAR struct ieee80211_iobuf_s *iob1,
                     FAR struct ieee80211_iobuf_s *iob2)
{
  unsigned int offset2;
  unsigned int ncopy;
  unsigned int navail;

  /* Find the last buffer in the iob1 buffer chain */
 
  while (iob1->m_link.flink)
    {
      iob1 = (FAR struct ieee80211_iobuf_s *)iob1->m_link.flink;
    }

  /* Then add data to the end of iob1 */

  offset2 = 0;
  while (iob2)
    {
      /* Is the iob1 tail buffer full? */

      if (iob1->m_len >= CONFIG_IEEE80211_BUFSIZE)
        {
          /* Yes.. Just connect the chains */

          iob1->m_link.flink = iob2->m_link.flink;

          /* Has the data offset in iob2? */

          if (offset2 > 0)
            {
              /* Yes, move the data down and adjust the size */

              iob2->m_len -= offset2;
              memcpy(iob2->m_data, &iob2->m_data[offset2], iob2->m_len);

              /* Set up to continue packing, but now into iob2 */

              iob1 = iob2;
              iob2 = (FAR struct ieee80211_iobuf_s *)iob2->m_link.flink;

              iob1->m_link.flink = NULL;
              offset2 = 0;
            }
          else
            {
              /* Otherwise, we are done */

              return;
            }
        }

      /* How many bytes can we copy from the source (iob2) */

      ncopy = iob2->m_len - offset2;

      /* Limit the size of the copy to the amount of free space in iob1 */

      navail = CONFIG_IEEE80211_BUFSIZE - iob1->m_len;
      if (ncopy > navail)
        {
          ncopy = navail;
        }

      /* Copy the data from iob2 into iob1 */

      memcpy(iob1->m_data + iob1->m_len, iob2->m_data, ncopy);
      iob1->m_len += ncopy;
      offset2 += ncopy;

      /* Have we consumed all of the data in iob2? */

      if (offset2 >= iob2->m_len)
        {
          /* Yes.. free iob2 and start processing the next I/O buffer
           * in the chain.
           */

          iob2 = ieee80211_iofree(iob2);
        }
    }
}
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "emulator.h"
#include "sr.h"

/* ******************************************************************
    Computer Network and Application COMP_SCI_4310
    Programming Assignment 2
    Reliable Transport with Selective Repeat
    
    by YUEN YUEN (a1836569)
    25 April 2023
**********************************************************************/

#define RTT  16.0       /* round trip time.  MUST BE SET TO 16.0 when submitting assignment */
#define WINDOWSIZE 6    /* the maximum number of buffered unacked packet */
#define NOTINUSE (-1)   /* used to fill header fields that are not being used */

/* generic procedure to compute the checksum of a packet.  Used by both sender and receiver  
   the simulator will overwrite part of your packet with 'z's.  It will not overwrite your 
   original checksum.  This procedure must generate a different checksum to the original if
   the packet is corrupted.
*/
int ComputeChecksum(struct pkt packet)
{
  int checksum = 0;
  int i;

  checksum = packet.seqnum;
  checksum += packet.acknum;
  for ( i=0; i<20; i++ ) 
    checksum += (int)(packet.payload[i]);

  return checksum;
}

bool IsCorrupted(struct pkt packet)
{
  if (packet.checksum == ComputeChecksum(packet))
    return (false);
  else
    return (true);
}

/* Buffer data sturcture for both A and B */
struct buffer {
  struct pkt packets[WINDOWSIZE];
  int size;
};

/* Insert packet into buffer order by packet number */
void insert_pkt(struct buffer *pkts, struct pkt new_pkt) {
  int i;

  if (pkts->size >= WINDOWSIZE)      
      return;  

  /* Check whether the packet exists */
  for (i = 0; i < pkts->size; i++) 
      if (pkts->packets[i].seqnum == new_pkt.seqnum)          
          return;

  i = pkts->size - 1;
  while (i >= 0 && pkts->packets[i].seqnum > new_pkt.seqnum) {
      pkts->packets[i + 1] = pkts->packets[i];
      i--;
  }

  pkts->packets[i + 1] = new_pkt;
  pkts->size++;
}

/* Remove packet by packet number and return it */
struct pkt remove_pkt(struct buffer *pkts, int seqnum) {
  int i;
  int index;
  struct pkt removed_pkt;
  memset(&removed_pkt, 0, sizeof(removed_pkt));

  if (pkts->size == 0)
      return removed_pkt;

  index = -1;
  for (i = 0; i < pkts->size; i++) {
      if (pkts->packets[i].seqnum == seqnum) {
      index = i;
      break;
      }
  }

  if (index == -1)       
      return removed_pkt;

  removed_pkt = pkts->packets[index];
  for (i = index; i < pkts->size - 1; i++) {
      pkts->packets[i] = pkts->packets[i + 1];
  }

  pkts->size--;
  return removed_pkt;
}

/* Print out the buffer elements */
void print_pkts(int type, struct buffer *pkts) {
  int i;

  printf("\n%s Data structure contains %d packet(s):\n", (type == A ? "A" : "B"), pkts->size);
  
  for (i = 0; i < pkts->size; i++) {
      printf("Buffer [%d]: seqnum: %d, acknum: %d, payload: %s\n",
          i, pkts->packets[i].seqnum, pkts->packets[i].acknum, pkts->packets[i].payload);
  }
}
/* End buffer data sturcture */


/********* Sender (A) variables and functions ************/

static struct buffer A_buffer;    /* data structure for storing packets waiting for ACK */
static int A_nextseqnum;          /* the next sequence number to be used by the sender */

/* called from layer 5 (application layer), passed the message to be sent to other side */
void A_output(struct msg message)
{
  struct pkt sendpkt;   /* packet send to layer 3 */
  int i;

  if ( A_buffer.size < WINDOWSIZE ) {
    if (TRACE > 1)
      printf("----A: New message arrives, send window is not full, send new messge to layer3!\n");

    /* create packet */
    sendpkt.seqnum = A_nextseqnum;
    sendpkt.acknum = NOTINUSE;
    for ( i=0; i<20 ; i++ ) 
      sendpkt.payload[i] = message.data[i];
    sendpkt.checksum = ComputeChecksum(sendpkt);

    /* put packet in A buffer */
    insert_pkt(&A_buffer, sendpkt);
    
    /* send out packet */
    if (TRACE > 0)
      printf("Sending packet %d to layer 3\n", sendpkt.seqnum);
    tolayer3 (A, sendpkt);

    /* start timer if first packet in window */
    if (A_buffer.size == 1)
      starttimer(A, RTT);

    /* get next sequence number, wrap back to 0 */
    A_nextseqnum++;

    /* Debug purprose */
    if (DEBUG > 0) {
      printf("A Next: %d\n", A_nextseqnum);
      print_pkts(A, &A_buffer);
    }
  } else {
    if (TRACE > 0)
      printf("----A: New message arrives, send window is full\n");
    window_full++;
  }
}

/* called from layer 3, when a packet arrives for layer 4 
   In this practical this will always be an ACK as B never sends data.
*/
void A_input(struct pkt packet)
{ 
  int i;

  /* if received ACK is not corrupted */ 
  if (!IsCorrupted(packet)) {
    if (TRACE > 0)
      printf("----A: uncorrupted ACK %d is received\n",packet.acknum);
    total_ACKs_received++;

    /* check if new ACK or duplicate */
    if (A_buffer.size != 0) {
      for (i = 0; i < A_buffer.size; i++) {
        if (A_buffer.packets[i].seqnum == packet.acknum) {
            /* packet is a new ACK */
            if (TRACE > 0)
              printf("----A: ACK %d is not a duplicate\n",packet.acknum);
            new_ACKs++;
            /* packet remove from buffer */
            remove_pkt(&A_buffer, packet.acknum);
        }
      }      
    } else {
        if (TRACE > 0)
          printf ("----A: duplicate ACK received, do nothing!\n");
    }

    /* Debug purprose */
    if (DEBUG > 0) {
      printf("A Next: %d\n", A_nextseqnum);
      print_pkts(A, &A_buffer);
    }
  } else {
    if (TRACE > 0)
      printf ("----A: corrupted ACK is received, do nothing!\n");
  }  
}

/* called when A's timer goes off */
void A_timerinterrupt(void)
{
  int i;

  if (TRACE > 0)
    printf("----A: time out,resend packets!\n");

  for (i = 0; i < A_buffer.size; i++) {
    if (TRACE > 0)
      printf ("---A: resending packet %d\n", A_buffer.packets[i].seqnum);

    /* resend packet */
    tolayer3(A, A_buffer.packets[i]);
    packets_resent++;
    
    /* set timer if buffer not empty */
    if (A_buffer.size > 0) starttimer(A, RTT);

    /* Debug purprose */
    if (DEBUG > 0) {
      printf("A Next: %d\n", A_nextseqnum);
      print_pkts(A, &A_buffer);
    }

    /* only resent first buffered packet */
    break;
  }
}

/* the following routine will be called once (only) before any other */
/* entity A routines are called. You can use it to do any initialization */
void A_init(void)
{
  A_nextseqnum = 0;
}

/********* Receiver (B)  variables and procedures ************/

static struct buffer B_buffer;    /* data structure for storing packets waiting for send to layer 5 */
static int B_nextseqnum;          /* the next sequence number to be used by the reciver */

/* called from layer 3, when a packet arrives for layer 4 at B*/
void B_input(struct pkt packet)
{
  struct pkt sendpkt;   /* packet send to layer 5 */
  struct pkt ackpkt;    /* ACK packet send to layer 3 */
  int i;

  /* create ACK packet */
  ackpkt.seqnum = packet.seqnum;
  ackpkt.acknum = packet.seqnum;
  for ( i=0; i<20 ; i++ ) 
    ackpkt.payload[i] = '0';
  ackpkt.checksum = ComputeChecksum(ackpkt); 
 
  /* if not corrupted and previous packet */
  if ( (!IsCorrupted(packet))  && packet.seqnum < B_nextseqnum) {
    if (TRACE > 0) 
        printf("----B: duplicate packet %d received, resend ACK!\n", B_nextseqnum);

    /* send out packet */
    tolayer3 (B, ackpkt);
  } 
  /* if not corrupted and received buffer not full */
  else if  ( (!IsCorrupted(packet))  && (B_buffer.size < WINDOWSIZE) ) {
    if (TRACE > 0)
      printf("----B: packet %d is correctly received, send ACK!\n", packet.seqnum);
    packets_received++;

    /* put packet in B buffer */
    packet.acknum = packet.seqnum;
    insert_pkt(&B_buffer, packet);

    /* scan expected packet, send it to layer 5 */
    while (B_buffer.packets[0].seqnum == B_nextseqnum) {
      sendpkt = remove_pkt(&B_buffer, B_nextseqnum);
      /* deliver to receiving application */
      tolayer5(B, sendpkt.payload);
      if (TRACE > 0) 
        printf("----B: packet %d delivered to layer 5\n", B_nextseqnum);
      B_nextseqnum++;
    }

    /* send out packet */
    tolayer3 (B, ackpkt);

    /* Debug purprose */
    if (DEBUG > 0) {
      printf("B Next: %d\n", B_nextseqnum);
      print_pkts(B, &B_buffer);
    }
  } else {
    /* packet is corrupted or out of order resend last ACK */
    if (TRACE > 0) 
      printf("----B: packet corrupted or reciver buffer is full, do nothing!\n");
  }  
}

/* the following routine will be called once (only) before any other */
/* entity B routines are called. You can use it to do any initialization */
void B_init(void)
{
  B_nextseqnum = 0;
}

/******************************************************************************
 * The following functions need be completed only for bi-directional messages *
 *****************************************************************************/

/* Note that with simplex transfer from a-to-B, there is no B_output() */
void B_output(struct msg message)  
{
}

/* called when B's timer goes off */
void B_timerinterrupt(void)
{
}


#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "emulator.h"
#include "sr.h"

#define RTT 16.0
#define WINDOWSIZE 6
#define NOTINUSE (-1)

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

/* buffer sorting */
void sort_send_buffer(struct pkt send_buffer[]) {
    int i, j;
    struct pkt temp;    

    for (i = 0; i < WINDOWSIZE - 1; i++) {    
        for (j = 0; j < WINDOWSIZE - 1 - i; j++) {
            if (send_buffer[j].seqnum >= 0 && send_buffer[j + 1].seqnum >= 0) {
                if (send_buffer[j].seqnum > send_buffer[j + 1].seqnum) {
                    temp = send_buffer[j];
                    send_buffer[j] = send_buffer[j + 1];
                    send_buffer[j + 1] = temp;                    
                }
            }
        }        
    }
}

/* Buffer data sturcture */
struct buffer {
    struct pkt packets[WINDOWSIZE];
    int size;
};

void insert_pkt(struct buffer *pkts, struct pkt new_pkt) {
    int i;

    if (pkts->size >= WINDOWSIZE) {
        printf("Error: Data structure is full\n");
        return;
    }

    i = pkts->size - 1;
    while (i >= 0 && pkts->packets[i].seqnum > new_pkt.seqnum) {
        pkts->packets[i + 1] = pkts->packets[i];
        i--;
    }

    pkts->packets[i + 1] = new_pkt;
    pkts->size++;
}

struct pkt remove_pkt(struct buffer *pkts, int seqnum) {
    int i;
    int index;
    struct pkt removed_pkt;
    memset(&removed_pkt, 0, sizeof(removed_pkt));

    if (pkts->size == 0) {
        printf("Error: Data structure is empty\n");
        return removed_pkt;
    }

    index = -1;
    for (i = 0; i < pkts->size; i++) {
        if (pkts->packets[i].seqnum == seqnum) {
        index = i;
        break;
        }
    }

    if (index == -1) {
        printf("Error: Packet not found\n");
        return removed_pkt;
    }

    removed_pkt = pkts->packets[index];
    for (i = index; i < pkts->size - 1; i++) {
        pkts->packets[i] = pkts->packets[i + 1];
    }

    pkts->size--;

    return removed_pkt;
}

void print_pkts(struct buffer *pkts) {
    int i;

    printf("Data structure contains %d packets:\n", pkts->size);
    for (i = 0; i < pkts->size; i++) {
        printf("Buffer [%d]: seqnum: %d, acknum: %d, payload: %s\n",
            i, pkts->packets[i].seqnum, pkts->packets[i].acknum, pkts->packets[i].payload);
    }
}
/* End buffer data sturcture */

/********* Sender (A) variables and functions ************/
static struct pkt send_buffer[WINDOWSIZE];
static int sendfirst, sendlast;
static int sendcount;
static int A_nextseqnum;

void A_init(void)
{   
    A_nextseqnum = 0;
    sendfirst = 0;
    sendlast = -1;
    sendcount = 0;
}

void A_output(struct msg message)
{
    struct pkt sendpkt;
    int i;

    if (sendcount < WINDOWSIZE) {
        if (TRACE > 1)
            printf("----A: New message arrives, send window is not full, send new messge to layer3!\n");

        sendpkt.seqnum = A_nextseqnum;        
        sendpkt.acknum = NOTINUSE;
        for (i = 0; i < 20 ; i++) 
            sendpkt.payload[i] = message.data[i];
        sendpkt.checksum = ComputeChecksum(sendpkt); 
        
        sendlast = (sendlast + 1) % WINDOWSIZE; 
        send_buffer[sendlast] = sendpkt;
        sendcount++;
        A_nextseqnum++;

        if (DEBUG > 0) {
            printf("Sending packet %d out\n", sendpkt.seqnum);
            printBuffer(A);
        }

        if (sendcount == 1)
            starttimer(A, RTT);

        if (TRACE > 0)
            printf("Sending packet %d to layer 3\n", sendpkt.seqnum);
        tolayer3 (A, sendpkt);
    } else {
        if (TRACE > 0)
            printf("----A: New message arrives, send window is full\n");
        window_full++;
    }
}

void A_input(struct pkt packet)
{
    /*  return;   */

    bool isNewAck = false;
    int i;

    if (!IsCorrupted(packet)) {
        if (TRACE > 0)
            printf("----A: uncorrupted ACK %d is received\n",packet.acknum);
        total_ACKs_received++;

        /*
        if (DEBUG > 0) {
            printf("==A== Recived ACK %d\n", packet.seqnum);
            printBuffer(A);
        }
        */

        for (i = sendfirst; i != sendlast; i = (i + 1) % WINDOWSIZE) {
            if (send_buffer[i].seqnum == packet.seqnum) {
                if (DEBUG > 0) {
                    printf("==A== Matched packet %d in buffer\n", packet.seqnum);                    
                }

                isNewAck = true;
                send_buffer[i].acknum = packet.acknum;
                break;
            }
        }

        if (isNewAck) {
            if (TRACE > 0)
              printf("----A: ACK %d is not a duplicate\n",packet.acknum);
            new_ACKs++;

            for (i = sendfirst; i != sendlast; i = (i + 1) % WINDOWSIZE) {
                if (send_buffer[i].seqnum == send_buffer[i].acknum) {
                    sendfirst = (i + 1) % WINDOWSIZE;
                    sendcount--;
                    stoptimer(A);
                    if (sendcount > 0) 
                        starttimer(A, RTT);
                    break;
                }
            }
        } else {
            if (TRACE > 0) 
                printf("----A: packet corrupted or not expected sequence number, resend ACK!\n");
        }

        if (DEBUG > 0) {
            printf("==A== After handling ACK %d\n", packet.seqnum);
            printBuffer(A);
        }
    } else {
        if (TRACE > 0)
            printf ("----A: corrupted ACK is received, do nothing!\n");
    }
}

void A_timerinterrupt(void)
{
    /*  return;   */

    int i;
    bool isResend = false;

    if (TRACE > 0)
        printf("----A: time out,resend packets!\n");

    for (i = sendfirst; i != sendlast; i = (i + 1) % WINDOWSIZE) {
        if (send_buffer[i].acknum == NOTINUSE) {
            if (TRACE > 0)
                printf ("---A: resending packet %d\n", send_buffer[i].seqnum);

            tolayer3(A, send_buffer[i]);
            packets_resent++;           
            isResend = true;
        }
    }

    if (isResend)
        starttimer(A, RTT);
}


/********* Receiver (B)  variables and procedures ************/

static struct pkt recv_buffer[WINDOWSIZE];
static int recvfirst, recvlast;
static int recvcount;
static int B_nextseqnum;

void B_init(void)
{
    int i;

    recvfirst = 0;
    recvlast = -1;
    recvcount = 0;
    B_nextseqnum = 0;    

    for ( i = 0; i < WINDOWSIZE; i++ ) {
        recv_buffer[i].seqnum = NOTINUSE;
        recv_buffer[i].acknum = NOTINUSE;
    }
}

void B_input(struct pkt packet) 
{
    struct pkt ackpkt;
    int i;
    bool isExistPkt = false;

    if (!IsCorrupted(packet)) {
        if (TRACE > 0)
            printf("----B: packet %d is correctly received, send ACK!\n",packet.seqnum);
        packets_received++;

        /*
        if (DEBUG > 0) {
            printf("Before packet %d handling\n", packet.seqnum);
            printBuffer(B);
        }
        */

        ackpkt.seqnum = packet.seqnum;
        ackpkt.acknum = packet.seqnum;
        for ( i=0; i<20 ; i++ ) 
            ackpkt.payload[i] = '0';
        ackpkt.checksum = ComputeChecksum(ackpkt);        

        if (recvlast > -1) {
            for (i = recvfirst; i != recvlast; i = (i + 1) % WINDOWSIZE) {
                if (recv_buffer[i].seqnum == packet.seqnum) {
                    isExistPkt = true;
                    break;
                }
            }
        }

        if (packet.seqnum == B_nextseqnum) {
            tolayer5(B, packet.payload);            
            B_nextseqnum++;

            if (DEBUG > 0) {
                printf("B: Packet %d sent to layer 5 <=======\n", packet.seqnum);
            }

            while (recvcount > 0 && recv_buffer[recvfirst].seqnum == B_nextseqnum) {
                struct pkt buffered_pkt = recv_buffer[recvfirst];
                tolayer5(B, buffered_pkt.payload);
                B_nextseqnum++;

                if (DEBUG > 0) {
                    printf("B: Packet %d sent to layer 5\n", packet.seqnum);
                }
                
                recvfirst = (recvfirst + 1) % WINDOWSIZE;                
                recvcount--;
            }            
        } else {
            if (DEBUG > 0)
                printf("B: packet %d is not expected packet %d\n", packet.seqnum, B_nextseqnum);

            if (recvlast > -1) {
                for (i = recvfirst; i != recvlast; i = (i + 1) % WINDOWSIZE) {
                    if (recv_buffer[i].seqnum == packet.seqnum) {
                        isExistPkt = true;
                        break;
                    }
                }
            }

            if (!isExistPkt) {
                recvlast = (recvlast + 1) % WINDOWSIZE;
                recv_buffer[recvlast] = packet;
                recvcount++;

                if (DEBUG > 0)
                    printf("B: packet %d is buffered\n", packet.seqnum);
            } else {
                if (DEBUG > 0)
                    printf("B: packet %d is deplicate\n", packet.seqnum);
            }
        }

        tolayer3(B, ackpkt);

        if (DEBUG > 0) {
            printf("After packet %d handling\n", packet.seqnum);
            printBuffer(B);
        }
    } else {
        if (TRACE > 0) 
            printf("----B: packet corrupted or not expected sequence number, resend ACK!\n");
    }
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



/********* Debug procedures ************/
void printBuffer(int BufferType)
{
    int i;

    printf("==%s== Buffer - [Start]: %d, [End]: %d, [Count]: %d, [Next]: %d\n", (BufferType == A ? "A" : "B"),
        (BufferType == A ? sendfirst : recvfirst), (BufferType == A ? sendlast : recvlast), (BufferType == A ? sendcount : recvcount), (BufferType == A ? A_nextseqnum : B_nextseqnum));

    for (i = 0; i < WINDOWSIZE; i++) {
        printf("Buffer[%d] - SEQ#: %d, ACK#: %d, Payload: '%s'\n", i, (BufferType == A ? send_buffer[i].seqnum : recv_buffer[i].seqnum), (BufferType == A ? send_buffer[i].acknum : recv_buffer[i].acknum), (BufferType == A ? send_buffer[i].payload : recv_buffer[i].payload));
    }
}
/********* End ebug procedures ************/
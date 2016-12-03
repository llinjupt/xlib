#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "xsocket.h"
#include "xipcmsg.h"
#include "xdebug.h"

#define CAP_MSG_MAGIC  0x36e31ea3

/* 
 * �ý��̼�ͨ�Ż��ƻ������µĿ��ǣ�
 * 1. һ���������(�߳�)������������(�߳�)
 * 2. ������Ӧ�ÿ���ֱ�ӷ������ݣ���ֱ�ӽ����������ݵ�ָ�봫�ݸ����ͺ���
 * 3. �������Ӧ�ÿ���ʶ��������ߣ������Խ������Ľ����������ȷ�ķ�����
 * 4. ������̲����첽���еķ�ʽ�Ծ����ܿ����Ӧ������
 * 
 * �������ϵĿ��ǣ������µ�˼����ѡ��
 * 1. ����ѡ��ķ�����UNIX Socket��UDP��TCP����Ϣ���У�ǰ���ֵı��ʶ���ͨ�����緢������
 * 2. ͨ�������׽��ֿ��Ժ����׵Ĵӽ��ܺ�����from������֪�Է��Ķ˿ںţ����ǲ�̫���ж�����
 *    �����̷��͵ģ�����Ϣ����ͨ����Ϣ����ID����������Ϣ��Ҳ�޷���÷��ͷ�����Ϣ����Ҫ��
 *    ��Ҫ����Ϣ�и���һ��ͷ��ipc_msghdr.
 * 3. ������⸽��һ��ͷ������ôͨ��send/sendto��������msgsnd��������ʱ��ֻ����Ϣ�Ŀ�ʼ
 *    ��ַ�������Ҫ��������һ��sizeof(ipc_msghdr) + msglen�Ŀռ䣬Ȼ�󿽱���Ϣ���µ���
 *    Ϣ���ڲ��������������ʹ��sendmsg��������ֱ��ʹ��io����������������Ϣ����
 * 4. �ڽ��նˣ������Ƚ���һ��sizeof(ipc_msghdr)���ȵ�ͷ����Ȼ���ͷ����ȡ��������������
 *    Ϣ�ĳ��ȣ�Ȼ���ٶ�̬���������Ϣ�Ŀռ�.
 */

int ipcmsg_server(const char *ip, int port)
{
  int fd = 0, ret = 0;
  
  if((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
  {
    perror("socket");
    return -1;
  }

  ret = setsockopt_reuseaddr(fd);
  if(ret < 0)
  {
    perror("setsockopt_reuseaddr");
    close(fd);
    return -1;
  }

  ret = xbind(fd, ip, port);
  if(ret < 0)
  {
    close(fd);
    return -1;
  }

  return fd;
}

int ipcmsg_connect(const char *ip, int port)
{
  int fd = 0, ret = 0;
  struct sockaddr_in sddr;
    
  if(ip == NULL)
    return -1;
  
  if((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
  {
    perror("socket");
    return -1;
  }
  
  memset(&sddr, 0, sizeof(sddr));
  sddr.sin_family = AF_INET;
  sddr.sin_port = htons(port);
  inet_aton(ip, &sddr.sin_addr);

  ret = connect(fd, (struct sockaddr *)&sddr, sizeof(sddr));
  if(ret < 0)
  {
    perror ("connect");
    close(fd);
    return -1;
  }

  return fd;
}

int ipcmsg_close(int fd)
{
  return close(fd);
}

int ipcmsg_send(int fd, struct sockaddr_in *caddr, 
                   void *data, unsigned int len)
{
  int sended = 0;
  ipcmsg_hdr_t ipcmsg_hdr;

  struct msghdr msg;
  struct iovec iov[2]; /* ipcmsg_hdr and data */
  
  if(data == NULL || len <= 0)
    return -1;

  /* may as a parameter and getpid only one time */
  //ipcmsg_hdr.pid = getpid();
  ipcmsg_hdr.len = htonl(len);
  
  iov[0].iov_base = &ipcmsg_hdr; 
  iov[0].iov_len = sizeof(ipcmsg_hdr_t);
  
  iov[1].iov_base = data; 
  iov[1].iov_len = len;
  
  memset(&msg, 0, sizeof(msg));
  if(caddr)
  {
    msg.msg_name = caddr;
    msg.msg_namelen = sizeof(struct sockaddr_in);
  }
  
  msg.msg_iov = iov;
  msg.msg_iovlen = 1;

  sended = sendmsg(fd, &msg, 0);
  if(sended != sizeof(ipcmsg_hdr_t))
    perror("sendmsg head");

  msg.msg_iov = iov + 1;
  msg.msg_iovlen = 1;
  sended = sendmsg(fd, &msg, 0);
  if(sended != len)
    perror("sendmsg data");

  return sended;
}

int ipcmsg_recv(int fd, struct sockaddr_in *caddr, void **data)
{
  int recv = 0;

  ipcmsg_hdr_t ipcmsg_hdr;

  struct sockaddr_in  _caddr;
  socklen_t  len = sizeof(_caddr);
  
  if(data == NULL || *data != NULL)
    return -1;

  recv = recvfrom(fd, &ipcmsg_hdr, sizeof(ipcmsg_hdr_t), 0,
                  (struct sockaddr *)&_caddr, &len);
  if(recv != sizeof(ipcmsg_hdr_t))
  {
    perror("recv ipcmsg hdr error");
    return -1;
  }

  if(caddr)
    memcpy(caddr, &_caddr, sizeof(struct sockaddr_in));
  
  ipcmsg_hdr.len = ntohl(ipcmsg_hdr.len);
  *data = malloc(ipcmsg_hdr.len);
  if(*data == NULL)
  {
    perror("malloc");
    return -1;
  }
  
  recv = recvfrom(fd, *data, ipcmsg_hdr.len, 0, NULL, NULL);
  if(recv != ipcmsg_hdr.len)
  {
    printf("recv not enouth data\n");
    free(*data);
    *data = NULL;
    return -1;
  }

  return recv;
}

int ipcmsg_write(int fd, void *data, unsigned int len)
{
  return write(fd, data, len);
}

int ipcmsg_tcp_server(const char *ip, int port)
{
  int fd = 0, ret = 0;

  if((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
  {
    perror("socket");
    return -1;
  }

  if(setsockopt_reuseaddr(fd) != 0)
  {
    perror("setsockopt_reuseaddr");
    close(fd);
    return -1;
  }
  
  ret = xbind(fd, ip, port);
  if(ret < 0)
  {
    perror("bind");
    close(fd);
    return -1;
  }

  if(listen(fd, 1) < 0) 
  {
    perror("listen");
    close(fd);
    return -1;
  }

  return fd;
}

/* timeout in sec for connect, if <= 0 then no affect */
int ipcmsg_tcp_connect(const char *ip, int port, int msec)
{
  int fd = 0, ret = 0;
  struct sockaddr_in sddr;
    
  if(ip == NULL)
    return -1;
  
  if((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
  {
    perror("socket");
    return -1;
  }
  
  memset(&sddr, 0, sizeof(sddr));
  sddr.sin_family = AF_INET;
  sddr.sin_port = htons(port);
  inet_aton(ip, &sddr.sin_addr);

  if(msec > 0)
  {
    struct timeval wait;
    wait.tv_sec = msec / 1000;
    wait.tv_usec = (msec - wait.tv_sec * 1000) * 1000;
    if(setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &wait,
                sizeof(struct timeval)) != 0)
    {
      perror("setsockopt timeout!\n");
      close(fd);
      return -1;
    }
  }

  ret = connect(fd, (struct sockaddr *)&sddr, sizeof(sddr));
  if(ret < 0)
  {
    perror("connect");
    close(fd);
    return -1;
  }

  return fd;
}

int ipcmsg_tcp_send(int fd, const void *buffer, unsigned int len)
{
  char *sendbuff = NULL;
  ipcmsg_tcp_hdr_t header;
  int header_len = sizeof(header);
  int ret = -1;

  header.magic = htonl(CAP_MSG_MAGIC);
  header.len = htonl(len);

  sendbuff = malloc(len + header_len);
  if(sendbuff == NULL)
  {
    perror("malloc");
    return -1;
  }

  memcpy(sendbuff, &header, header_len);
  memcpy(sendbuff + header_len, buffer, len);

  ret = send(fd, sendbuff, header_len + len, 0);
  if(ret != header_len + len)
    ret = -1;
  else 
    ret = len;

  free(sendbuff);
  return ret;
}

int ipcmsg_tcp_recv(int fd, void *buffer, unsigned int bufflen)
{
  int ret = -1;
  int nread = 0;
  int nleft = 0;

  ipcmsg_tcp_hdr_t header;
  
  ret = recv(fd, &header, sizeof(header), 0);
  if(ret == 0)
    return 0;
  
  if(ret < 0)
  {
    xlogerr("recv failed %s\n", strerror(errno));
    return -1;
  }
  else if(ret != sizeof(header))
  {
    xlogerr("Message incomplete\n");
    return -1;
  }

  if(ntohl(header.magic) != CAP_MSG_MAGIC)
  {
    xlogerr("Magic error %08x\n", ntohl(header.magic));
    return -1;
  }

  if(ntohl(header.len) > bufflen)
  {
    xlogerr("Message too long %d\n", ntohl(header.len));
    return -1;
  }

  nleft = ntohl(header.len);
  nread = 0;
  while(nleft > 0)
  {
    ret = recv(fd, buffer + nread, nleft, 0);  
    if(ret < 0)
    {
      xlogerr("recv failed %s\n", strerror(errno));
      return -1;
    }
    else if(ret == 0)
    {
      return 0;
    }

    nleft -= ret;
    nread += ret;
  }

  return nread;
}

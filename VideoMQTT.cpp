
#include "VideoMQTT.h"

VideoMQTT::VideoMQTT(std::string _host, std::string _topic_cmd, 
					std::string _topic_status, int _port, int _QoS) 
						: OMXThread() , mosqpp::mosquittopp()
{
	host 		= _host ;
	topic_cmd 	= _topic_cmd;
	topic_status = _topic_status ;
	port = _port;
	QoS  = _QoS;
	gethostname(client_id,50);
	mosqpp::lib_init();
	sprintf(client_id,"%s_omx",client_id);
	reinitialise(client_id,true);
	printf("VideoMQTT client_id:%s\n",client_id);
	connect(host.c_str(), port, 120);
	subscribe(NULL,topic_cmd.c_str(),QoS);
	
	if (dbus_connect() < 0)
	{
		CLog::Log(LOGWARNING, "VideoMQTT: DBus connection failed");
	} 
	else 
	{
		CLog::Log(LOGDEBUG, "VideoMQTT: DBus connection succeeded");
	}

	dbus_threads_init_default();
	Create();
}

VideoMQTT::~VideoMQTT() 
{
	mosqpp::lib_cleanup();
	Close();
}

void VideoMQTT::Close()
{
  if (ThreadHandle()) 
  {
    StopThread();
  }
  dbus_disconnect();
}


void VideoMQTT::Sleep(unsigned int dwMilliSeconds)
{
  struct timespec req;
  req.tv_sec = dwMilliSeconds / 1000;
  req.tv_nsec = (dwMilliSeconds % 1000) * 1000000;

  while ( nanosleep(&req, &req) == -1 && errno == EINTR && (req.tv_nsec > 0 || req.tv_sec > 0));
}

void VideoMQTT::Process() 
{
  errorCount = 0; // Just to be sure
  
//  loop_start();

  while(!m_bStop)
  {
    if (conn)
      dbus_connection_read_write_dispatch(conn, 0);
	
	//loop_forever(100,1);

	switch (int a=loop(10,1) )
	{
	case MOSQ_ERR_SUCCESS:
		errorCount = 0; // Reset errorCount
		//fprintf(stderr,"MQTT loop : everything is working\n");
		break;
	case MOSQ_ERR_INVAL:
		errorCount++;
		fprintf(stderr,"Error MQTT loop : the input parameters were invalid\n");
		exit(EXIT_FAILURE);
		break;
	case MOSQ_ERR_NOMEM:
		errorCount++;
		fprintf(stderr,"Error MQTT loop : out of memory condition\n");
		break;
	case MOSQ_ERR_NO_CONN:
		errorCount++;
		reconnect();
		fprintf(stderr,"Error MQTT loop : the client isn’t connected to a broker\n");
		break;
	case MOSQ_ERR_CONN_LOST:
		errorCount++;
		while(a)
		{
			fprintf(stderr,"Error MQTT loop : the connection to the broker was lost\n");
			//disconnect();
			Sleep(10);
			//reinitialise(client_id,true);
			a = reconnect();
			fprintf(stderr,"Attempting to reconnect. connect() returned : %d \n",a);
			Sleep(500);
		}
		subscribe(NULL,topic_cmd.c_str(),QoS);
		break;
	case MOSQ_ERR_PROTOCOL:
		errorCount++;
		fprintf(stderr,"Error MQTT loop : protocol error communicating with the broker\n");
		break;
	case MOSQ_ERR_ERRNO:
		errorCount++;
		fprintf(stderr,"Error MQTT loop : system call returned an error\n");
		perror("");
		break;
	}
	
	/*if(errorCount > MAXERRORCOUNT)
	{
		fprintf(stderr,"Error MQTT loop : MAX ERROR COUNT REACHED\n");
		exit(EXIT_FAILURE);
	}*/
	
    Sleep(20);
  }
}

void VideoMQTT::on_message(const struct mosquitto_message *message)
{
//printf("on_message mqtt \n");
//	std::string topic = std::string((const char *)message->topic);
//	if(strcmp(topic.c_str(),topic_cmd.c_str()))
//	{// The message was not for us
//		return;
//	}

	std::string mess = std::string((const char *)message->payload);
	mess.append(" ");
	size_t pos = 0;
	std::string cmd[5];
	uint8_t	i=0;

	

	while( (pos=mess.find(" ")) != std::string::npos)
	{
		cmd[i]=mess.substr(0,pos);
		mess.erase(0,pos+1);
		//fprintf(stderr,"%s_ ",cmd[i].c_str());
		i++;
	}
	//fprintf(stderr,"\n");
  printf("%s\n",mess.c_str());
  
  if(i==0)
  {
  	return;
  }
  else if(i==1) // only a command, no args
  {
	if(!strcmp(cmd[0].c_str(),"reset"))
	{
		int64_t a=0;
		send_dbus_cmd("SetPosition", a);
	  	send_dbus_cmd("Play");
	  	Sleep(1000);
		send_dbus_cmd("UnHideVideo");
		send_dbus_cmd("Pause");
	} else 
	{
		send_dbus_cmd(cmd[0].c_str());
	}
  }
  else if(i==2) // One arg
  {
 	if(!strcmp(cmd[0].c_str(),"SetPosition"))
	{
		send_dbus_cmd(cmd[0].c_str(), (int64_t)(std::stoull(cmd[1])*1000000));
  	}  
	else if(!strcmp(cmd[0].c_str(),"setPosition"))
	{
		send_dbus_cmd("SetPosition", (int64_t)(std::stoull(cmd[1])*1000000));
	  	send_dbus_cmd("Play");
	  	Sleep(1000);
  	} 
  }
}

void VideoMQTT::send_MQTT_msg(std::string *mess)
{
	const char * message = mess->c_str();
	//printf("%s : %d\n",message,mess->size());
	publish(NULL,topic_status.c_str(),mess->size(),message, QoS, 0);
}

void VideoMQTT::send_dbus_cmd(const char * cmd)
{
	DBusMessage *message = NULL, *reply = NULL;
	DBusError error;
	
	if (!conn)
		return;

  dbus_error_init(&error);

  if (!(message = dbus_message_new_method_call(m_dbus_name.c_str(),
                                              OMXPLAYER_DBUS_PATH_SERVER, 
                                              OMXPLAYER_DBUS_INTERFACE_PLAYER,
                                              cmd))) 
  {
    CLog::Log(LOGWARNING, "VideoMQTT: DBus error 1");
    goto fail;
  }

  reply = dbus_connection_send_with_reply_and_block(conn, message, -1, &error);

  if (!reply || dbus_error_is_set(&error))
    goto fail;

  dbus_message_unref(message);
  dbus_message_unref(reply);

  return;

fail:
printf("dbus fail: ");
  if (dbus_error_is_set(&error)) 
  {
    printf("%s", error.message);
    dbus_error_free(&error);
  }

  if (message)
    dbus_message_unref(message);

  if (reply)
    dbus_message_unref(reply);
}

void VideoMQTT::send_dbus_cmd(const char * cmd, int32_t arg)
{
	DBusMessage *message = NULL, *reply = NULL;
	DBusError error;
	
	if (!conn)
		return;

  dbus_error_init(&error);

  if (!(message = dbus_message_new_method_call(m_dbus_name.c_str(),
                                              OMXPLAYER_DBUS_PATH_SERVER, 
                                              OMXPLAYER_DBUS_INTERFACE_PLAYER,
                                              cmd))) 
  {
    CLog::Log(LOGWARNING, "VideoMQTT: DBus error 1");
    goto fail;
  }
  
  dbus_message_append_args(message, DBUS_TYPE_INT32, &arg, DBUS_TYPE_INVALID);

  reply = dbus_connection_send_with_reply_and_block(conn, message, -1, &error);

  if (!reply || dbus_error_is_set(&error))
    goto fail;

  dbus_message_unref(message);
  dbus_message_unref(reply);

  return;

fail:
printf("dbus32 fail: ");
  if (dbus_error_is_set(&error)) 
  {
    printf("%s", error.message);
    dbus_error_free(&error);
  }

  if (message)
    dbus_message_unref(message);

  if (reply)
    dbus_message_unref(reply);
}

void VideoMQTT::send_dbus_cmd(const char * cmd, int64_t arg)
{
	DBusMessage *message = NULL, *reply = NULL;
	DBusError error;
	const char *oPath = "/not/used";
	
	if (!conn)
		return;

  dbus_error_init(&error);

  if (!(message = dbus_message_new_method_call(m_dbus_name.c_str(),
                                              OMXPLAYER_DBUS_PATH_SERVER, 
                                              OMXPLAYER_DBUS_INTERFACE_PLAYER,
                                              cmd))) 
  {
    CLog::Log(LOGWARNING, "VideoMQTT: DBus error 1");
    goto fail;
  }
	
  dbus_message_append_args(message, DBUS_TYPE_OBJECT_PATH, &oPath,
  									DBUS_TYPE_INT64, &arg,
  									DBUS_TYPE_INVALID);

  reply = dbus_connection_send_with_reply_and_block(conn, message, -1, &error);

  if (!reply || dbus_error_is_set(&error))
    goto fail;

  dbus_message_unref(message);
  dbus_message_unref(reply);

  return;

fail:
printf("dbus64 fail: ");
  if (dbus_error_is_set(&error)) 
  {
    printf("%s", error.message);
    dbus_error_free(&error);
  }

  if (message)
    dbus_message_unref(message);

  if (reply)
    dbus_message_unref(reply);
}

void VideoMQTT::setDbusName(std::string dbus_name)
{
  m_dbus_name = dbus_name;
}

int VideoMQTT::dbus_connect() 
{
  DBusError error;

  dbus_error_init(&error);
  if (!(conn = dbus_bus_get_private(DBUS_BUS_SESSION, &error))) 
  {
    CLog::Log(LOGWARNING, "dbus_bus_get_private(): %s", error.message);
        goto fail;
  }

  dbus_connection_set_exit_on_disconnect(conn, FALSE);

  return 0;

fail:
    if (dbus_error_is_set(&error))
        dbus_error_free(&error);

    if (conn) 
    {
        dbus_connection_close(conn);
        dbus_connection_unref(conn);
        conn = NULL;
    }

    return -1;

}

void VideoMQTT::dbus_disconnect() 
{
    if (conn) 
    {
        dbus_connection_close(conn);
        dbus_connection_unref(conn);
        conn = NULL;
    }
}

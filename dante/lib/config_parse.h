typedef union {
	char	*string;
	uid_t	uid;
} YYSTYPE;
#define	SERVERCONFIG	258
#define	CLIENTCONFIG	259
#define	DEPRECATED	260
#define	CLIENTRULE	261
#define	INTERNAL	262
#define	EXTERNAL	263
#define	DEBUGING	264
#define	RESOLVEPROTOCOL	265
#define	SRCHOST	266
#define	NOMISMATCH	267
#define	NOUNKNOWN	268
#define	EXTENSION	269
#define	BIND	270
#define	PRIVILEGED	271
#define	IOTIMEOUT	272
#define	CONNECTTIMEOUT	273
#define	METHOD	274
#define	NONE	275
#define	GSSAPI	276
#define	UNAME	277
#define	COMPATIBILITY	278
#define	REUSEADDR	279
#define	SAMEPORT	280
#define	USERNAME	281
#define	USER_PRIVILEGED	282
#define	USER_UNPRIVILEGED	283
#define	USER_LIBWRAP	284
#define	LOGOUTPUT	285
#define	LOGFILE	286
#define	ROUTE	287
#define	VIA	288
#define	VERDICT_BLOCK	289
#define	VERDICT_PASS	290
#define	PROTOCOL	291
#define	PROTOCOL_TCP	292
#define	PROTOCOL_UDP	293
#define	PROTOCOL_FAKE	294
#define	PROXYPROTOCOL	295
#define	PROXYPROTOCOL_SOCKS_V4	296
#define	PROXYPROTOCOL_SOCKS_V5	297
#define	PROXYPROTOCOL_MSPROXY_V2	298
#define	COMMAND	299
#define	COMMAND_BIND	300
#define	COMMAND_CONNECT	301
#define	COMMAND_UDPASSOCIATE	302
#define	COMMAND_BINDREPLY	303
#define	ACTION	304
#define	LINE	305
#define	LIBWRAPSTART	306
#define	OPERATOR	307
#define	LOG	308
#define	LOG_CONNECT	309
#define	LOG_DATA	310
#define	LOG_DISCONNECT	311
#define	LOG_ERROR	312
#define	LOG_IOOPERATION	313
#define	IPADDRESS	314
#define	DOMAIN	315
#define	DIRECT	316
#define	PORT	317
#define	PORTNUMBER	318
#define	SERVICENAME	319
#define	NUMBER	320
#define	FROM	321
#define	TO	322


extern YYSTYPE socks_yylval;
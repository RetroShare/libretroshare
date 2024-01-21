# RetroShare decentralized communication platform
#
# Copyright (C) 2021  Gioacchino Mazzurco <gio@eigenlab.org>
# Copyright (C) 2021  Asociación Civil Altermundi <info@altermundi.net>
#
# SPDX-License-Identifier: CC0-1.0

list(
	APPEND RS_PUBLIC_HEADERS
	retroshare/rsexpr.h
	retroshare/rsgxsdistsync.h
	retroshare/rsiface.h
	retroshare/rsrtt.h
	retroshare/rsbanlist.h
	retroshare/rsconfig.h
	retroshare/rsdisc.h
	retroshare/rsflags.h
	retroshare/rsgrouter.h
	retroshare/rsgxsflags.h
	retroshare/rsgxsservice.h
	retroshare/rsgxstrans.h
	retroshare/rsgxstunnel.h
	retroshare/rsids.h
	retroshare/rsnotify.h
	retroshare/rsphoto.h
	retroshare/rsplugin.h
	retroshare/rsreputations.h
	retroshare/rsservicecontrol.h
	retroshare/rstokenservice.h
	retroshare/rsturtle.h	
	retroshare/rsgossipdiscovery.h
	retroshare/rsgxscommon.h
	retroshare/rsposted.h
	retroshare/rsstatus.h
	retroshare/rsversion.h
	retroshare/rsgxsifacehelper.h
	retroshare/rshistory.h
	retroshare/rsidentity.h
	retroshare/rsmsgs.h
	retroshare/rsgxschannels.h
	retroshare/rsgxscircles.h
	retroshare/rsgxsiface.h
	retroshare/rsgxsifacetypes.h
	retroshare/rstypes.h
	retroshare/rsgxsforums.h
	retroshare/rsevents.h
	retroshare/rsfiles.h
	retroshare/rsinit.h
	retroshare/rspeers.h )

list(
	APPEND RS_SOURCES
	chat/distantchat.cc
	chat/p3chatservice.cc
	chat/rschatitems.cc
	chat/distributedchat.cc )

list(
	APPEND RS_IMPLEMENTATION_HEADERS
	chat/distantchat.h
	chat/distributedchat.h
	chat/p3chatservice.h
	chat/rschatitems.h )

list(
	APPEND RS_SOURCES
	crypto/chacha20.cpp
	crypto/hashstream.cc
	crypto/rsaes.cc
	crypto/rscrypto.cpp )

list(
	APPEND RS_IMPLEMENTATION_HEADERS
	crypto/chacha20.h
	crypto/hashstream.h
	crypto/rsaes.h
	crypto/rscrypto.h )

if(RS_BITDHT)
	list(
		APPEND RS_PUBLIC_HEADERS
		retroshare/rsdht.h )

	list(
		APPEND RS_SOURCES
		dht/connectstatebox.cc
		dht/p3bitdht.cc
		dht/p3bitdht_interface.cc
		dht/p3bitdht_peernet.cc
		dht/p3bitdht_peers.cc
		dht/p3bitdht_relay.cc )

	list(
		APPEND RS_IMPLEMENTATION_HEADERS
		dht/connectstatebox.h
		dht/p3bitdht.h
		dht/stunaddrassist.h )
endif(RS_BITDHT)

list(
	APPEND RS_SOURCES
	file_sharing/filelist_io.cc
	file_sharing/rsfilelistitems.cc
	file_sharing/file_tree.cc
	file_sharing/directory_updater.cc
	file_sharing/p3filelists.cc
	file_sharing/hash_cache.cc
	file_sharing/dir_hierarchy.cc
	file_sharing/directory_storage.cc
	ft/ftchunkmap.cc
	ft/ftfilecreator.cc
	ft/ftfileprovider.cc
	ft/ftfilesearch.cc
	ft/ftturtlefiletransferitem.cc
	ft/fttransfermodule.cc
	ft/ftcontroller.cc
	ft/ftdatamultiplex.cc
	ft/ftextralist.cc
	ft/ftserver.cc )

list(
	APPEND RS_IMPLEMENTATION_HEADERS
	file_sharing/directory_list.h
	file_sharing/directory_storage.h
	file_sharing/directory_updater.h
	file_sharing/dir_hierarchy.h
	file_sharing/filelist_io.h
	file_sharing/file_sharing_defaults.h
	file_sharing/hash_cache.h
	file_sharing/p3filelists.h
	file_sharing/rsfilelistitems.h
	ft/ftchunkmap.h
	ft/ftcontroller.h
	ft/ftdata.h
	ft/ftdatamultiplex.h
	ft/ftextralist.h
	ft/ftfilecreator.h
	ft/ftfileprovider.h
	ft/ftfilesearch.h
	ft/ftsearch.h
	ft/ftserver.h
	ft/fttransfermodule.h
	ft/ftturtlefiletransferitem.h )

list(
	APPEND RS_SOURCES
	grouter/groutermatrix.cc
	grouter/grouteritems.cc
	grouter/p3grouter.cc )

list(
	APPEND RS_IMPLEMENTATION_HEADERS
	grouter/groutercache.h
	grouter/grouterclientservice.h
	grouter/grouteritems.h
	grouter/groutermatrix.h
	grouter/groutertypes.h
	grouter/p3grouter.h )

list(
	APPEND RS_SOURCES
	gxs/rsgxsdata.cc
	gxs/rsgxsrequesttypes.cc
	gxs/gxssecurity.cc
	gxs/gxstokenqueue.cc
	gxs/rsdataservice.cc
	gxs/rsgxsdataaccess.cc
	gxs/rsgxsnetutils.cc
	gxs/rsgxsnettunnel.cc
	gxs/rsgxsutil.cc
	gxs/rsnxsobserver.cpp
	gxs/rsgenexchange.cc
	gxs/rsgxsnetservice.cc )

list(
	APPEND RS_IMPLEMENTATION_HEADERS
	gxs/gxssecurity.h
	gxs/gxstokenqueue.h
	gxs/rsdataservice.h
	gxs/rsgds.h
	gxs/rsgenexchange.h
	gxs/rsgixs.h
	gxs/rsgroups.h
	gxs/rsgxsdataaccess.h
	gxs/rsgxsdata.h
	gxs/rsgxs.h
	gxs/rsgxsnetservice.h
	gxs/rsgxsnettunnel.h
	gxs/rsgxsnetutils.h
	gxs/rsgxsnotify.h
	gxs/rsgxsrequesttypes.h
	gxs/rsgxsutil.h
	gxs/rsnxs.h
	gxs/rsnxsobserver.h )

list(
	APPEND RS_SOURCES
	gxstrans/p3gxstransitems.cc
	gxstrans/p3gxstrans.cc )

list(
	APPEND RS_IMPLEMENTATION_HEADERS
	gxstrans/p3gxstrans.h
	gxstrans/p3gxstransitems.h )

list(
	APPEND RS_SOURCES
	gxstunnel/rsgxstunnelitems.cc
	gxstunnel/p3gxstunnel.cc )

list(
	APPEND RS_IMPLEMENTATION_HEADERS
	gxstunnel/p3gxstunnel.h
	gxstunnel/rsgxstunnelitems.h )

if(RS_JSON_API)
	list(
		APPEND RS_PUBLIC_HEADERS
		retroshare/rsjsonapi.h )

	list(
		APPEND RS_SOURCES
		jsonapi/jsonapi.cpp )

	list(
		APPEND RS_IMPLEMENTATION_HEADERS
		jsonapi/jsonapi.h
		jsonapi/jsonapiitems.h )
endif(RS_JSON_API)

list(
	APPEND RS_SOURCES
	pgp/openpgpsdkhandler.cc
	pgp/pgpauxutils.cc
	pgp/pgphandler.cc
	pgp/pgpkeyutil.cc
	pgp/rscertificate.cc )

list(
	APPEND RS_IMPLEMENTATION_HEADERS
	pgp/openpgpsdkhandler.h
	pgp/pgpauxutils.h
	pgp/pgphandler.h
	pgp/pgpkeyutil.h
	pgp/rscertificate.h )

#./plugins/dlfcn_win32.cc
#./plugins/dlfcn_win32.h
#./plugins/pluginmanager.h
#./plugins/rscacheservice.h
#./plugins/rspqiservice.h

list(
	APPEND RS_SOURCES
	plugins/pluginmanager.cc )

list(
	APPEND RS_IMPLEMENTATION_HEADERS
	plugins/pluginmanager.h )

list(
	APPEND RS_SOURCES
	pqi/pqibin.cc
	pqi/pqiipset.cc
	pqi/pqiloopback.cc
	pqi/pqimonitor.cc
	pqi/pqipersongrp.cc
	pqi/pqiqos.cc
	pqi/pqiqosstreamer.cc
	pqi/pqisslproxy.cc
	pqi/pqistore.cc
	pqi/authgpg.cc
	pqi/p3cfgmgr.cc
	pqi/p3notify.cc
	pqi/p3servicecontrol.cc
	pqi/pqifdbin.cc
	pqi/pqinetstatebox.cc
	pqi/pqiperson.cc
	pqi/pqiservice.cc
	pqi/pqissllistener.cc
	pqi/pqissludp.cc
	pqi/pqithreadstreamer.cc
	pqi/sslfns.cc
	pqi/authssl.cc
	pqi/p3historymgr.cc
	pqi/p3linkmgr.cc
	pqi/pqihandler.cc
	pqi/pqistreamer.cc
	pqi/p3netmgr.cc
	pqi/p3peermgr.cc
	pqi/pqinetwork.cc
	pqi/pqissl.cc
	pqi/pqisslpersongrp.cc )

list(
	APPEND RS_IMPLEMENTATION_HEADERS
	pqi/authgpg.h
	pqi/authssl.h
	pqi/p3cfgmgr.h
	pqi/p3historymgr.h
	pqi/p3linkmgr.h
	pqi/p3netmgr.h
	pqi/p3notify.h
	pqi/p3peermgr.h
	pqi/p3servicecontrol.h
	pqi/p3upnpmgr.h
	pqi/pqiassist.h
	pqi/pqi_base.h
	pqi/pqibin.h
	pqi/pqifdbin.h
	pqi/pqi.h
	pqi/pqihandler.h
	pqi/pqihash.h
	pqi/pqiindic.h
	pqi/pqiipset.h
	pqi/pqilistener.h
	pqi/pqiloopback.h
	pqi/pqimonitor.h
	pqi/pqinetstatebox.h
	pqi/pqinetwork.h
	pqi/pqipersongrp.h
	pqi/pqiperson.h
	pqi/pqiqos.h
	pqi/pqiqosstreamer.h
	pqi/pqiservice.h
	pqi/pqiservicemonitor.h
	pqi/pqissl.h
	pqi/pqissllistener.h
	pqi/pqisslpersongrp.h
	pqi/pqisslproxy.h
	pqi/pqissludp.h
	pqi/pqistore.h
	pqi/pqistreamer.h
	pqi/pqithreadstreamer.h
	pqi/sslfns.h )

#./pqi/pqissli2psam3.cpp
#./pqi/pqissli2psam3.h

list(
	APPEND RS_SOURCES
	rsitems/rsbanlistitems.cc
	rsitems/rsbwctrlitems.cc
	rsitems/rsconfigitems.cc
	rsitems/rsfiletransferitems.cc
	rsitems/rsgxscommentitems.cc
	rsitems/rsgxsforumitems.cc
	rsitems/rsgxsiditems.cc
	rsitems/rsgxsrecognitems.cc
	rsitems/rsgxsreputationitems.cc
	rsitems/rsgxsupdateitems.cc
	rsitems/rshistoryitems.cc
	rsitems/rsrttitems.cc
	rsitems/rsserviceinfoitems.cc )

#retroshare/rswiki.h
#./rsitems/rswikiitems.cc
#./rsitems/rswikiitems.h
#./rsitems/rswireitems.h

list(
	APPEND RS_SOURCES
	rsitems/rsgxschannelitems.cc
	rsitems/rsgxscircleitems.cc
	rsitems/rsgxsitems.cc
	rsitems/rsmsgitems.cc )

#./rsitems/rsphotoitems.cc
#./rsitems/rsphotoitems.h
#./rsitems/rsposteditems.h
#./rsitems/rswireitems.cc
#retroshare/rswire.h

list(
	APPEND RS_SOURCES
	rsitems/rsnxsitems.cc )

list(
	APPEND RS_IMPLEMENTATION_HEADERS
	rsitems/itempriorities.h
	rsitems/rsbanlistitems.h
	rsitems/rsbwctrlitems.h
	rsitems/rsconfigitems.h
	rsitems/rsfiletransferitems.h
	rsitems/rsgxschannelitems.h
	rsitems/rsgxscircleitems.h
	rsitems/rsgxscommentitems.h
	rsitems/rsgxsforumitems.h
	rsitems/rsgxsiditems.h
	rsitems/rsgxsitems.h
	rsitems/rsgxsrecognitems.h
	rsitems/rsgxsreputationitems.h
	rsitems/rsgxsupdateitems.h
	rsitems/rsheartbeatitems.h
	rsitems/rshistoryitems.h
	rsitems/rsitem.h
	rsitems/rsmsgitems.h
	rsitems/rsnxsitems.h
	rsitems/rspluginitems.h
	rsitems/rsposteditems.h
	rsitems/rsrttitems.h
	rsitems/rsserviceids.h
	rsitems/rsserviceinfoitems.h
	rsitems/rsstatusitems.h )

list(
	APPEND RS_SOURCES
	rsserver/p3status.cc
	rsserver/p3face-config.cc
	rsserver/p3face-info.cc
	rsserver/p3history.cc
	rsserver/p3serverconfig.cc
	rsserver/rsloginhandler.cc
	rsserver/p3face-server.cc
	rsserver/p3msgs.cc
	rsserver/p3peers.cc
	rsserver/rsaccounts.cc
	rsserver/rsinit.cc )

list(
	APPEND RS_IMPLEMENTATION_HEADERS
	rsserver/p3face.h
	rsserver/p3history.h
	rsserver/p3msgs.h
	rsserver/p3peers.h
	rsserver/p3serverconfig.h
	rsserver/p3status.h
	rsserver/rsaccounts.h
	rsserver/rsloginhandler.h )

list(
	APPEND RS_SOURCES
	serialiser/rsbaseserial.cc
	serialiser/rsserializable.cc
	serialiser/rstlvaddrs.cc
	serialiser/rstlvbanlist.cc
	serialiser/rstlvbase.cc
	serialiser/rstlvbinary.cc
	serialiser/rstlvfileitem.cc
	serialiser/rstlvgenericmap.inl
	serialiser/rstlvgenericparam.cc
	serialiser/rstlvidset.cc
	serialiser/rstlvimage.cc
	serialiser/rstlvitem.cc
	serialiser/rstlvkeys.cc
	serialiser/rstlvkeyvalue.cc
	serialiser/rstlvstring.cc
	serialiser/rsserializer.cc
	serialiser/rstypeserializer.cc
	serialiser/rsserial.cc )

list(
	APPEND RS_IMPLEMENTATION_HEADERS
	serialiser/rsbaseserial.h
	serialiser/rsserial.h
	serialiser/rsserializable.h
	serialiser/rsserializer.h
	serialiser/rstlvaddrs.h
	serialiser/rstlvbanlist.h
	serialiser/rstlvbase.h
	serialiser/rstlvbinary.h
	serialiser/rstlvfileitem.h
	serialiser/rstlvgenericmap.h
	serialiser/rstlvgenericmap.inl
	serialiser/rstlvgenericparam.h
	serialiser/rstlvidset.h
	serialiser/rstlvimage.h
	serialiser/rstlvitem.h
	serialiser/rstlvkeys.h
	serialiser/rstlvkeyvalue.h
	serialiser/rstlvlist.h
	serialiser/rstlvmaps.h
	serialiser/rstlvstring.h
	serialiser/rstypeserializer.h )

# ./services/autoproxy
#./services/autoproxy/p3i2psam3.cpp
#./services/autoproxy/p3i2psam3.h

list(
	APPEND RS_SOURCES
	services/autoproxy/rsautoproxymonitor.cc
	services/p3bwctrl.cc
	services/p3heartbeat.cc
	services/p3service.cc
	services/p3serviceinfo.cc
	services/p3statusservice.cc
	services/p3banlist.cc
	services/p3rtt.cc
	services/rseventsservice.cc
	services/p3gxscircles.cc
	services/p3gxscommon.cc
	services/p3gxsreputation.cc
	services/p3msgservice.cc
	services/p3idservice.cc
	services/p3gxschannels.cc
	services/p3gxsforums.cc )

list(
	APPEND RS_IMPLEMENTATION_HEADERS
	services/p3banlist.h
	services/p3bwctrl.h
	services/p3gxschannels.h
	services/p3gxscircles.h
	services/p3gxscommon.h
	services/p3gxsforums.h
	services/p3gxsreputation.h
	services/p3heartbeat.h
	services/p3idservice.h
	services/p3msgservice.h
	services/p3postbase.h
	services/p3posted.h
	services/p3rtt.h
	services/p3service.h
	services/p3serviceinfo.h
	services/p3statusservice.h
	services/rseventsservice.h )
	
#./services/p3wiki.cc
#./services/p3wiki.h
#./services/p3wire.cc
#./services/p3wire.h

#./services/p3photoservice.cc
#./services/p3photoservice.h

list(
	APPEND RS_SOURCES
	services/p3postbase.cc
	services/p3posted.cc
	rsitems/rsposteditems.cc )

if(RS_BRODCAST_DISCOVERY)
	list(
		APPEND RS_PUBLIC_HEADERS
		retroshare/rsbroadcastdiscovery.h )

	list(
		APPEND RS_SOURCES
		services/broadcastdiscoveryservice.cc )

	list(
		APPEND RS_IMPLEMENTATION_HEADERS
		services/broadcastdiscoveryservice.h )
endif(RS_BRODCAST_DISCOVERY)

list(
	APPEND RS_SOURCES
	tcponudp/tcppacket.cc
	tcponudp/tcpstream.cc
	tcponudp/tou.cc
	tcponudp/udppeer.cc
	tcponudp/bss_tou.cc
	tcponudp/udprelay.cc )

list(
	APPEND RS_IMPLEMENTATION_HEADERS
	tcponudp/bio_tou.h
	tcponudp/rsudpstack.h
	tcponudp/tcppacket.h
	tcponudp/tcpstream.h
	tcponudp/tou.h
	tcponudp/udppeer.h
	tcponudp/udprelay.h )

if(RS_BITDHT_STUNNER)
	list(
		APPEND RS_SOURCES
		tcponudp/udpstunner.cc )

	list(
		APPEND RS_IMPLEMENTATION_HEADERS
		tcponudp/udpstunner.h )
endif(RS_BITDHT_STUNNER)

list(
	APPEND RS_SOURCES
	turtle/rsturtleitem.cc
	turtle/p3turtle.cc )

list(
	APPEND RS_IMPLEMENTATION_HEADERS
	turtle/p3turtle.h
	turtle/rsturtleitem.h
	turtle/turtleclientservice.h
	turtle/turtlestatistics.h
	turtle/turtletypes.h )

list(
	APPEND RS_SOURCES
	util/contentvalue.cc
	util/rsdbbind.cc
	util/rsdiscspace.cc
	util/rsexpr.cc
	util/rsprint.cc
	util/rsrecogn.cc
	util/rstickevent.cc
	util/rstime.cc
	util/smallobject.cc
	util/retrodb.cc
	util/rsbase64.cc
	util/rsjson.cc
	util/rskbdinput.cc
	util/rsrandom.cc
	util/rsstring.cc
	util/rsurl.cc
	util/folderiterator.cc
	util/rsdir.cc
	util/rsfile.cc
	util/dnsresolver.cc
	util/extaddrfinder.cc
	util/rsdebug.cc
	util/rsdnsutils.cc
	util/rsnet.cc
	util/rsnet_ss.cc
	util/rsstacktrace.cc
	util/rsthreads.cc )

# util/i2pcommon.cpp
# util/i2pcommon.h

list(
	APPEND RS_IMPLEMENTATION_HEADERS
	util/argstream.h
	util/contentvalue.h
	util/cxx11retrocompat.h
	util/cxx14retrocompat.h
	util/cxx17retrocompat.h
	util/cxx23retrocompat.h
	util/dnsresolver.h
	util/extaddrfinder.h
	util/folderiterator.h
	util/largefile_retrocompat.hpp
	util/radix32.h
	util/radix64.h
	util/retrodb.h
	util/rsbase64.h
	util/rsdbbind.h
	util/rsdebug.h
	util/rsdebuglevel0.h
	util/rsdebuglevel1.h
	util/rsdebuglevel2.h
	util/rsdebuglevel3.h
	util/rsdebuglevel4.h
	util/rsdeprecate.h
	util/rsdir.h
	util/rsdiscspace.h
	util/rserrorbubbleorexit.h
	util/rsendian.h
	util/rsfile.h
	util/rsinitedptr.h
	util/rsjson.h
	util/rskbdinput.cc
	util/rskbdinput.h
	util/rslikelyunlikely.h
	util/rsmacrosugar.hpp
	util/rsmemcache.h
	util/rsmemory.h
	util/rsnet.h
	util/rsprint.h
	util/rsrandom.h
	util/rsrecogn.h
	util/rsstd.h
	util/rsstring.h
	util/rsthreads.cc
	util/rsthreads.h
	util/rstickevent.h
	util/rstime.h
	util/rsurl.h
	util/rswin.h
	util/smallobject.h
	util/stacktrace.h )

if(RS_FORUM_DEEP_INDEX)
	list(
		APPEND RS_SOURCES
		deep_search/commonutils.cpp
		deep_search/forumsindex.cpp )

	list(
		APPEND RS_IMPLEMENTATION_HEADERS
		deep_search/commonutils.hpp
		deep_search/forumsindex.hpp )
endif(RS_FORUM_DEEP_INDEX)


#./deep_search/filesflacindexer.hpp
#./deep_search/filesoggindexer.hpp
#./deep_search/filestaglibindexer.hpp
#./deep_search/filesindex.cpp
#./deep_search/filesindex.hpp
#./deep_search/channelsindex.cpp
#./deep_search/channelsindex.hpp

list(
	APPEND RS_SOURCES
	gossipdiscovery/gossipdiscoveryitems.cc
	gossipdiscovery/p3gossipdiscovery.cc )

list(
	APPEND RS_IMPLEMENTATION_HEADERS
	gossipdiscovery/gossipdiscoveryitems.h
	gossipdiscovery/p3gossipdiscovery.h )

if(RS_MINIUPNPC)
	list(
		APPEND RS_SOURCES
		rs_upnp/upnphandler_miniupnp.cc
		rs_upnp/upnputil.cc )

	list(
		APPEND RS_IMPLEMENTATION_HEADERS
		rs_upnp/upnphandler_miniupnp.h
		rs_upnp/upnputil.h )
endif(RS_MINIUPNPC)

#./rs_upnp/UPnPBase.cpp
#./rs_upnp/upnphandler_libupnp.cc
#./rs_upnp/upnptest.cc

#if(CMAKE_SYSTEM_NAME STREQUAL "Android")
if(RS_ANDROID)
	list(
		APPEND RS_SOURCES
		rs_android/errorconditionwrap.cpp
		rs_android/retroshareserviceandroid.cpp
		rs_android/rsjni.cpp )

	list(
		APPEND RS_IMPLEMENTATION_HEADERS
		rs_android/androidcoutcerrcatcher.hpp
		rs_android/ifaddrs-android.h
		rs_android/LocalArray.h
		rs_android/retroshareserviceandroid.hpp
		rs_android/rsjni.hpp
		rs_android/ScopedFd.h )
endif()

list(
	APPEND RS_SOURCES
	pqi/rstcpsocket.cc
	pqi/pqiproxy.cc
	tor/AddOnionCommand.cpp
	tor/AuthenticateCommand.cpp
	tor/CryptoKey.cpp
	tor/GetConfCommand.cpp
	tor/HiddenService.cpp
	tor/PendingOperation.cpp
	tor/ProtocolInfoCommand.cpp
	tor/SetConfCommand.cpp
	tor/StrUtil.cpp
	tor/TorControl.cpp
	tor/TorControlCommand.cpp
	tor/TorControlSocket.cpp
	tor/TorProcess.cpp
	tor/TorManager.cpp )

list(
	APPEND RS_IMPLEMENTATION_HEADERS
	pqi/pqiproxy.h
	pqi/rstcpsocket.h
	tor/AddOnionCommand.h
	tor/AuthenticateCommand.h
	tor/bytearray.h
	tor/CryptoKey.h
	tor/GetConfCommand.h
	tor/HiddenService.h
	tor/PendingOperation.h
	tor/ProtocolInfoCommand.h
	tor/SetConfCommand.h
	tor/StrUtil.h
	tor/TorControlCommand.h
	tor/TorControl.h
	tor/TorControlSocket.h
	tor/TorManager.h
	tor/TorProcess.h
	tor/TorTypes.h
	tor/Useful.h )

if(RS_WEBUI)
	list(
		APPEND RS_PUBLIC_HEADERS
		retroshare/rswebui.h )

	list(
		APPEND RS_SOURCES
		jsonapi/p3webui.cc )

	list(
		APPEND RS_IMPLEMENTATION_HEADERS
		jsonapi/p3webui.h )
endif(RS_WEBUI)

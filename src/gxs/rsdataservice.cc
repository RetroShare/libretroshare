/*******************************************************************************
 * libretroshare/src/gxs: gxsdataservice.cc                                    *
 *                                                                             *
 * libretroshare: retroshare core library                                      *
 *                                                                             *
 * Copyright 2011-2011 by Evi-Parker Christopher                               *
 *                                                                             *
 * This program is free software: you can redistribute it and/or modify        *
 * it under the terms of the GNU Lesser General Public License as              *
 * published by the Free Software Foundation, either version 3 of the          *
 * License, or (at your option) any later version.                             *
 *                                                                             *
 * This program is distributed in the hope that it will be useful,             *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of              *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the                *
 * GNU Lesser General Public License for more details.                         *
 *                                                                             *
 * You should have received a copy of the GNU Lesser General Public License    *
 * along with this program. If not, see <https://www.gnu.org/licenses/>.       *
 *                                                                             *
 *******************************************************************************/

/*****
 * #define RS_DATA_SERVICE_DEBUG       1
 * #define RS_DATA_SERVICE_DEBUG_TIME  1
 * #define RS_DATA_SERVICE_DEBUG_CACHE 1
 ****/

#include <fstream>
#include <util/rsdir.h>
#include <algorithm>

#ifdef RS_DATA_SERVICE_DEBUG_TIME
#include <util/rstime.h>
#endif

#include "rsdataservice.h"
#include "retroshare/rsgxsflags.h"
#include "util/rsstring.h"

#define MSG_TABLE_NAME std::string("MESSAGES")
#define GRP_TABLE_NAME std::string("GROUPS")
#define DATABASE_RELEASE_TABLE_NAME std::string("DATABASE_RELEASE")

#define GRP_LAST_POST_UPDATE_TRIGGER std::string("LAST_POST_UPDATE")

#define MSG_INDEX_GRPID std::string("INDEX_MESSAGES_GRPID")

// generic
#define KEY_NXS_DATA        std::string("nxsData")
#define KEY_NXS_DATA_LEN    std::string("nxsDataLen")
#define KEY_NXS_IDENTITY    std::string("identity")
#define KEY_GRP_ID          std::string("grpId")
#define KEY_ORIG_GRP_ID     std::string("origGrpId")
#define KEY_PARENT_GRP_ID   std::string("parentGrpId")
#define KEY_SIGN_SET        std::string("signSet")
#define KEY_TIME_STAMP      std::string("timeStamp")
#define KEY_NXS_FLAGS       std::string("flags")
#define KEY_NXS_META        std::string("meta")
#define KEY_NXS_SERV_STRING std::string("serv_str")
#define KEY_NXS_HASH        std::string("hash")
#define KEY_RECV_TS         std::string("recv_time_stamp")

// remove later
#define KEY_NXS_FILE_OLD std::string("nxsFile")
#define KEY_NXS_FILE_OFFSET_OLD std::string("fileOffset")
#define KEY_NXS_FILE_LEN_OLD std::string("nxsFileLen")

// grp table columns
#define KEY_KEY_SET std::string("keySet")
#define KEY_GRP_NAME std::string("grpName")
#define KEY_GRP_SIGN_FLAGS std::string("signFlags")
#define KEY_GRP_CIRCLE_ID std::string("circleId")
#define KEY_GRP_CIRCLE_TYPE std::string("circleType")
#define KEY_GRP_INTERNAL_CIRCLE std::string("internalCircle")
#define KEY_GRP_ORIGINATOR std::string("originator")
#define KEY_GRP_AUTHEN_FLAGS std::string("authenFlags")

// grp local
#define KEY_GRP_SUBCR_FLAG std::string("subscribeFlag")
#define KEY_GRP_POP std::string("popularity")
#define KEY_MSG_COUNT std::string("msgCount")
#define KEY_GRP_STATUS std::string("grpStatus")
#define KEY_GRP_LAST_POST std::string("lastPost")
#define KEY_GRP_REP_CUTOFF std::string("rep_cutoff")

// msg table columns
#define KEY_MSG_ID std::string("msgId")
#define KEY_ORIG_MSG_ID std::string("origMsgId")
#define KEY_MSG_PARENT_ID std::string("parentId")
#define KEY_MSG_THREAD_ID std::string("threadId")
#define KEY_MSG_NAME std::string("msgName")

// msg local
#define KEY_MSG_STATUS      std::string("msgStatus")
#define KEY_CHILD_TS        std::string("childTs")

// database release columns
#define KEY_DATABASE_RELEASE_ID std::string("id")
#define KEY_DATABASE_RELEASE_ID_VALUE 1
#define KEY_DATABASE_RELEASE std::string("release")

const std::string RsGeneralDataService::GRP_META_SERV_STRING = KEY_NXS_SERV_STRING;
const std::string RsGeneralDataService::GRP_META_STATUS = KEY_GRP_STATUS;
const std::string RsGeneralDataService::GRP_META_SUBSCRIBE_FLAG = KEY_GRP_SUBCR_FLAG;
const std::string RsGeneralDataService::GRP_META_CUTOFF_LEVEL = KEY_GRP_REP_CUTOFF;
const std::string RsGeneralDataService::GRP_META_LAST_POST = KEY_GRP_LAST_POST;

const std::string RsGeneralDataService::MSG_META_SERV_STRING = KEY_NXS_SERV_STRING;
const std::string RsGeneralDataService::MSG_META_STATUS = KEY_MSG_STATUS;

const uint32_t RsGeneralDataService::GXS_MAX_ITEM_SIZE = 1572864; // 1.5 Mbytes

static int addColumn(std::list<std::string> &list, const std::string &attribute)
{
    list.push_back(attribute);
    return list.size() - 1;
}

RsDataService::RsDataService(const std::string &serviceDir, const std::string &dbName, uint16_t serviceType,
                             RsGxsSearchModule * /* mod */, const std::string& key)
    : RsGeneralDataService(), mDbMutex("RsDataService"), mServiceDir(serviceDir), mDbName(dbName), mDbPath(mServiceDir + "/" + dbName), mServType(serviceType), mDb(NULL)
{
    bool isNewDatabase = !RsDirUtil::fileExists(mDbPath);

    mDb = new RetroDb(mDbPath, RetroDb::OPEN_READWRITE_CREATE, key);
    mUseCache = true;

    initialise(isNewDatabase);

    // for retrieving msg meta
    mColMsgMeta_GrpId         = addColumn(mMsgMetaColumns, KEY_GRP_ID);
    mColMsgMeta_TimeStamp     = addColumn(mMsgMetaColumns, KEY_TIME_STAMP);
    mColMsgMeta_NxsFlags      = addColumn(mMsgMetaColumns, KEY_NXS_FLAGS);
    mColMsgMeta_SignSet       = addColumn(mMsgMetaColumns, KEY_SIGN_SET);
    mColMsgMeta_NxsIdentity   = addColumn(mMsgMetaColumns, KEY_NXS_IDENTITY);
    mColMsgMeta_NxsHash       = addColumn(mMsgMetaColumns, KEY_NXS_HASH);
    mColMsgMeta_MsgId         = addColumn(mMsgMetaColumns, KEY_MSG_ID);
    mColMsgMeta_OrigMsgId     = addColumn(mMsgMetaColumns, KEY_ORIG_MSG_ID);
    mColMsgMeta_MsgStatus     = addColumn(mMsgMetaColumns, KEY_MSG_STATUS);
    mColMsgMeta_ChildTs       = addColumn(mMsgMetaColumns, KEY_CHILD_TS);
    mColMsgMeta_MsgParentId   = addColumn(mMsgMetaColumns, KEY_MSG_PARENT_ID);
    mColMsgMeta_MsgThreadId   = addColumn(mMsgMetaColumns, KEY_MSG_THREAD_ID);
    mColMsgMeta_Name          = addColumn(mMsgMetaColumns, KEY_MSG_NAME);
    mColMsgMeta_NxsServString = addColumn(mMsgMetaColumns, KEY_NXS_SERV_STRING);
    mColMsgMeta_RecvTs        = addColumn(mMsgMetaColumns, KEY_RECV_TS);
    mColMsgMeta_NxsDataLen    = addColumn(mMsgMetaColumns, KEY_NXS_DATA_LEN);

    // for retrieving actual data
    mColMsg_GrpId = addColumn(mMsgColumns, KEY_GRP_ID);
    mColMsg_NxsData = addColumn(mMsgColumns, KEY_NXS_DATA);
    mColMsg_MetaData = addColumn(mMsgColumns, KEY_NXS_META);
    mColMsg_MsgId = addColumn(mMsgColumns, KEY_MSG_ID);

    // for retrieving msg data with meta
    mMsgColumnsWithMeta = mMsgColumns;
    mColMsg_WithMetaOffset = mMsgColumnsWithMeta.size();
    mMsgColumnsWithMeta.insert(mMsgColumnsWithMeta.end(), mMsgMetaColumns.begin(), mMsgMetaColumns.end());

    // for retrieving grp meta data
    mColGrpMeta_GrpId       = addColumn(mGrpMetaColumns, KEY_GRP_ID);
    mColGrpMeta_TimeStamp   = addColumn(mGrpMetaColumns, KEY_TIME_STAMP);
    mColGrpMeta_NxsFlags    = addColumn(mGrpMetaColumns, KEY_NXS_FLAGS);
//    mColGrpMeta_SignSet = addColumn(mGrpMetaColumns, KEY_SIGN_SET);
    mColGrpMeta_NxsIdentity = addColumn(mGrpMetaColumns, KEY_NXS_IDENTITY);
    mColGrpMeta_NxsHash     = addColumn(mGrpMetaColumns, KEY_NXS_HASH);
    mColGrpMeta_KeySet      = addColumn(mGrpMetaColumns, KEY_KEY_SET);
    mColGrpMeta_SubscrFlag  = addColumn(mGrpMetaColumns, KEY_GRP_SUBCR_FLAG);
    mColGrpMeta_Pop         = addColumn(mGrpMetaColumns, KEY_GRP_POP);
    mColGrpMeta_MsgCount    = addColumn(mGrpMetaColumns, KEY_MSG_COUNT);
    mColGrpMeta_Status      = addColumn(mGrpMetaColumns, KEY_GRP_STATUS);
    mColGrpMeta_Name        = addColumn(mGrpMetaColumns, KEY_GRP_NAME);
    mColGrpMeta_LastPost    = addColumn(mGrpMetaColumns, KEY_GRP_LAST_POST);
    mColGrpMeta_OrigGrpId   = addColumn(mGrpMetaColumns, KEY_ORIG_GRP_ID);
    mColGrpMeta_ServString  = addColumn(mGrpMetaColumns, KEY_NXS_SERV_STRING);
    mColGrpMeta_SignFlags   = addColumn(mGrpMetaColumns, KEY_GRP_SIGN_FLAGS);
    mColGrpMeta_CircleId    = addColumn(mGrpMetaColumns, KEY_GRP_CIRCLE_ID);
    mColGrpMeta_CircleType  = addColumn(mGrpMetaColumns, KEY_GRP_CIRCLE_TYPE);
    mColGrpMeta_InternCircle = addColumn(mGrpMetaColumns, KEY_GRP_INTERNAL_CIRCLE);
    mColGrpMeta_Originator  = addColumn(mGrpMetaColumns, KEY_GRP_ORIGINATOR);
    mColGrpMeta_AuthenFlags = addColumn(mGrpMetaColumns, KEY_GRP_AUTHEN_FLAGS);
    mColGrpMeta_ParentGrpId = addColumn(mGrpMetaColumns, KEY_PARENT_GRP_ID);
    mColGrpMeta_RecvTs      = addColumn(mGrpMetaColumns, KEY_RECV_TS);
    mColGrpMeta_RepCutoff   = addColumn(mGrpMetaColumns, KEY_GRP_REP_CUTOFF);
    mColGrpMeta_NxsDataLen  = addColumn(mGrpMetaColumns, KEY_NXS_DATA_LEN);

    // for retrieving actual grp data
    mColGrp_GrpId = addColumn(mGrpColumns, KEY_GRP_ID);
    mColGrp_NxsData = addColumn(mGrpColumns, KEY_NXS_DATA);
    mColGrp_MetaData = addColumn(mGrpColumns, KEY_NXS_META);

    // for retrieving grp data with meta
    mGrpColumnsWithMeta = mGrpColumns;
    mColGrp_WithMetaOffset = mGrpColumnsWithMeta.size();
    mGrpColumnsWithMeta.insert(mGrpColumnsWithMeta.end(), mGrpMetaColumns.begin(), mGrpMetaColumns.end());

    // Group id columns
    mColGrpId_GrpId = addColumn(mGrpIdColumn, KEY_GRP_ID);

    // Msg id columns
    mColMsgId_MsgId = addColumn(mMsgIdColumn, KEY_MSG_ID);
}

RsDataService::~RsDataService(){

#ifdef RS_DATA_SERVICE_DEBUG
    std::cerr << "RsDataService::~RsDataService()";
    std::cerr << std::endl;
#endif

    mDb->closeDb();
    delete mDb;
}

static bool moveDataFromFileToDatabase(RetroDb *db, const std::string serviceDir, const std::string &tableName, const std::string &keyId, std::list<std::string> &files)
{
    bool ok = true;

    // Move message data
    std::list<std::string> columns;
    columns.push_back(keyId);
    columns.push_back(KEY_NXS_FILE_OLD);
    columns.push_back(KEY_NXS_FILE_OFFSET_OLD);
    columns.push_back(KEY_NXS_FILE_LEN_OLD);

    RetroCursor* c = db->sqlQuery(tableName, columns, "", "");

    if (c)
    {
        bool valid = c->moveToFirst();

        while (ok && valid){
            std::string dataFile;
            c->getString(1, dataFile);

            if (!dataFile.empty()) {
                bool fileOk = true;

                // first try to find the file in the service dir
                if (RsDirUtil::fileExists(serviceDir + "/" + dataFile)) {
                    dataFile.insert(0, serviceDir + "/");
                } else if (RsDirUtil::fileExists(dataFile)) {
                    // use old way for backward compatibility
                    //TODO: can be removed later
                } else {
                    fileOk = false;

                    std::cerr << "moveDataFromFileToDatabase() cannot find file " << dataFile;
                    std::cerr << std::endl;
                }

                if (fileOk) {
                    std::string id;
                    c->getString(0, id);

                    uint32_t offset = c->getInt32(2);
                    uint32_t data_len = c->getInt32(3);

                    char* data = new char[data_len];
                    std::ifstream istrm(dataFile.c_str(), std::ios::binary);
                    istrm.seekg(offset, std::ios::beg);
                    istrm.read(data, data_len);
                    istrm.close();

                    ContentValue cv;
                    // insert new columns
                    cv.put(KEY_NXS_DATA, data_len, data);
                    cv.put(KEY_NXS_DATA_LEN, (int32_t) data_len);
                    // clear old columns
                    cv.put(KEY_NXS_FILE_OLD, "");
                    cv.put(KEY_NXS_FILE_OFFSET_OLD, 0);
                    cv.put(KEY_NXS_FILE_LEN_OLD, 0);

                    ok = db->sqlUpdate(tableName, keyId + "='" + id + "'", cv);
                    delete[] data;

                    if (std::find(files.begin(), files.end(), dataFile) == files.end()) {
                        files.push_back(dataFile);
                    }
                }
            }

            valid = c->moveToNext();
        }

        delete c;
    }

    return ok;
}

void RsDataService::initialise(bool isNewDatabase)
{
    const int databaseRelease = 1;
    int currentDatabaseRelease = 0;
    bool ok = true;

    RsStackMutex stack(mDbMutex);

    // initialise database

    if (isNewDatabase || !mDb->tableExists(DATABASE_RELEASE_TABLE_NAME)) {
        // create table for database release
        mDb->execSQL("CREATE TABLE " + DATABASE_RELEASE_TABLE_NAME + "(" +
                     KEY_DATABASE_RELEASE_ID + " INT PRIMARY KEY," +
                     KEY_DATABASE_RELEASE + " INT);");
    }

    if (isNewDatabase) {
        // create table for msg data
        mDb->execSQL("CREATE TABLE " + MSG_TABLE_NAME + "(" +
                     KEY_MSG_ID + " TEXT PRIMARY KEY," +
                     KEY_GRP_ID +  " TEXT," +
                     KEY_NXS_FLAGS + " INT,"  +
                     KEY_ORIG_MSG_ID +  " TEXT," +
                     KEY_TIME_STAMP + " INT," +
                     KEY_NXS_IDENTITY + " TEXT," +
                     KEY_SIGN_SET + " BLOB," +
                     KEY_NXS_DATA + " BLOB,"+
                     KEY_NXS_DATA_LEN + " INT," +
                     KEY_MSG_STATUS + " INT," +
                     KEY_CHILD_TS + " INT," +
                     KEY_NXS_META + " BLOB," +
                     KEY_MSG_THREAD_ID + " TEXT," +
                     KEY_MSG_PARENT_ID + " TEXT,"+
                     KEY_MSG_NAME + " TEXT," +
                     KEY_NXS_SERV_STRING + " TEXT," +
                     KEY_NXS_HASH + " TEXT," +
                     KEY_RECV_TS + " INT);");

        // create table for grp data
        mDb->execSQL("CREATE TABLE " + GRP_TABLE_NAME + "(" +
                     KEY_GRP_ID + " TEXT PRIMARY KEY," +
                     KEY_TIME_STAMP + " INT," +
                     KEY_NXS_DATA + " BLOB," +
                     KEY_NXS_DATA_LEN + " INT," +
                     KEY_KEY_SET + " BLOB," +
                     KEY_NXS_META + " BLOB," +
                     KEY_GRP_NAME + " TEXT," +
                     KEY_GRP_LAST_POST + " INT," +
                     KEY_GRP_POP + " INT," +
                     KEY_MSG_COUNT + " INT," +
                     KEY_GRP_SUBCR_FLAG + " INT," +
                     KEY_GRP_STATUS + " INT," +
                     KEY_NXS_IDENTITY + " TEXT," +
                     KEY_ORIG_GRP_ID + " TEXT," +
                     KEY_NXS_SERV_STRING + " TEXT," +
                     KEY_NXS_FLAGS + " INT," +
                     KEY_GRP_AUTHEN_FLAGS + " INT," +
                     KEY_GRP_SIGN_FLAGS + " INT," +
                     KEY_GRP_CIRCLE_ID + " TEXT," +
                     KEY_GRP_CIRCLE_TYPE + " INT," +
                     KEY_GRP_INTERNAL_CIRCLE + " TEXT," +
                     KEY_GRP_ORIGINATOR + " TEXT," +
                     KEY_NXS_HASH + " TEXT," +
                     KEY_RECV_TS + " INT," +
                     KEY_PARENT_GRP_ID + " TEXT," +
                     KEY_GRP_REP_CUTOFF + " INT," +
                     KEY_SIGN_SET + " BLOB);");

        mDb->execSQL("CREATE TRIGGER " + GRP_LAST_POST_UPDATE_TRIGGER +
                " INSERT ON " + MSG_TABLE_NAME +
                std::string(" BEGIN ") +
                " UPDATE " + GRP_TABLE_NAME + " SET " + KEY_GRP_LAST_POST + "= new."
                + KEY_RECV_TS + " WHERE " + KEY_GRP_ID + "=new." + KEY_GRP_ID + ";"
                + std::string("END;"));

        mDb->execSQL("CREATE INDEX " + MSG_INDEX_GRPID + " ON " + MSG_TABLE_NAME + "(" + KEY_GRP_ID +  ");");

        // Insert release, no need to upgrade
        ContentValue cv;
        cv.put(KEY_DATABASE_RELEASE_ID, KEY_DATABASE_RELEASE_ID_VALUE);
        cv.put(KEY_DATABASE_RELEASE, databaseRelease);
        mDb->sqlInsert(DATABASE_RELEASE_TABLE_NAME, "", cv);

        currentDatabaseRelease = databaseRelease;
    } else {
        // check release

        {
            // try to select the release
            std::list<std::string> columns;
            columns.push_back(KEY_DATABASE_RELEASE);

            std::string where;
            rs_sprintf(where, "%s=%d", KEY_DATABASE_RELEASE_ID.c_str(), KEY_DATABASE_RELEASE_ID_VALUE);

            RetroCursor* c = mDb->sqlQuery(DATABASE_RELEASE_TABLE_NAME, columns, where, "");
            if (c) {
                ok = c->moveToFirst();

                if (ok) {
                    currentDatabaseRelease = c->getInt32(0);
                }
                delete c;

                if (!ok) {
                    // No record found ... insert the record
                    ContentValue cv;
                    cv.put(KEY_DATABASE_RELEASE_ID, KEY_DATABASE_RELEASE_ID_VALUE);
                    cv.put(KEY_DATABASE_RELEASE, currentDatabaseRelease);
                    ok = mDb->sqlInsert(DATABASE_RELEASE_TABLE_NAME, "", cv);
                }
            } else {
                ok = false;
            }
        }

        // Release 1
        int newRelease = 1;
        if (ok && currentDatabaseRelease < newRelease) {
            // Update database
            std::list<std::string> files;

            ok = startReleaseUpdate(newRelease);

            // Move data in files into database
            ok = ok && mDb->execSQL("ALTER TABLE " + GRP_TABLE_NAME + " ADD COLUMN " + KEY_NXS_DATA + " BLOB;");
            ok = ok && mDb->execSQL("ALTER TABLE " + GRP_TABLE_NAME + " ADD COLUMN " + KEY_NXS_DATA_LEN + " INT;");
            ok = ok && mDb->execSQL("ALTER TABLE " + MSG_TABLE_NAME + " ADD COLUMN " + KEY_NXS_DATA + " BLOB;");
            ok = ok && mDb->execSQL("ALTER TABLE " + MSG_TABLE_NAME + " ADD COLUMN " + KEY_NXS_DATA_LEN + " INT;");

            ok = ok && moveDataFromFileToDatabase(mDb, mServiceDir, GRP_TABLE_NAME, KEY_GRP_ID, files);
            ok = ok && moveDataFromFileToDatabase(mDb, mServiceDir, MSG_TABLE_NAME, KEY_MSG_ID, files);

// SQLite doesn't support DROP COLUMN
//            ok = ok && mDb->execSQL("ALTER TABLE " + GRP_TABLE_NAME + " DROP COLUMN " + KEY_NXS_FILE_OLD + ";");
//            ok = ok && mDb->execSQL("ALTER TABLE " + GRP_TABLE_NAME + " DROP COLUMN " + KEY_NXS_FILE_OFFSET_OLD + ";");
//            ok = ok && mDb->execSQL("ALTER TABLE " + GRP_TABLE_NAME + " DROP COLUMN " + KEY_NXS_FILE_LEN_OLD + ";");
//            ok = ok && mDb->execSQL("ALTER TABLE " + MSG_TABLE_NAME + " DROP COLUMN " + KEY_NXS_FILE_OLD + ";");
//            ok = ok && mDb->execSQL("ALTER TABLE " + MSG_TABLE_NAME + " DROP COLUMN " + KEY_NXS_FILE_OFFSET_OLD + ";");
//            ok = ok && mDb->execSQL("ALTER TABLE " + MSG_TABLE_NAME + " DROP COLUMN " + KEY_NXS_FILE_LEN_OLD + ";");

            ok = finishReleaseUpdate(newRelease, ok);
            if (ok) {
                // Remove transfered files
                std::list<std::string>::const_iterator file;
                for (file = files.begin(); file != files.end(); ++file) {
                    remove(file->c_str());
                }
                currentDatabaseRelease = newRelease;
            }
        }
    }

    if (ok) {
        std::cerr << "Database " << mDbName << " release " << currentDatabaseRelease << " successfully initialised." << std::endl;
    } else {
        std::cerr << "Database " << mDbName << " initialisation failed." << std::endl;
    }
}

bool RsDataService::startReleaseUpdate(int release)
{
    // Update database
    std::cerr << "Database " << mDbName << " update to release " << release << "." << std::endl;

    return mDb->beginTransaction();
}

bool RsDataService::finishReleaseUpdate(int release, bool result)
{
    if (result) {
        std::string where;
        rs_sprintf(where, "%s=%d", KEY_DATABASE_RELEASE_ID.c_str(), KEY_DATABASE_RELEASE_ID_VALUE);

        ContentValue cv;
        cv.put(KEY_DATABASE_RELEASE, release);
        result = mDb->sqlUpdate(DATABASE_RELEASE_TABLE_NAME, where, cv);
    }

    if (result) {
        result = mDb->commitTransaction();
    } else {
        result = mDb->rollbackTransaction();
    }

    if (result) {
        std::cerr << "Database " << mDbName << " successfully updated to release " << release << "." << std::endl;
    } else {
        std::cerr << "Database " << mDbName << " update to release " << release << "failed." << std::endl;
    }

    return result;
}

std::shared_ptr<RsGxsGrpMetaData> RsDataService::locked_getGrpMeta(RetroCursor& c, int colOffset)
{
#ifdef RS_DATA_SERVICE_DEBUG
    std::cerr << "RsDataService::locked_getGrpMeta()" << std::endl;
#endif

    bool ok = true;

    // for extracting raw data
    uint32_t offset = 0;
    char* data = NULL;
    uint32_t data_len = 0;

    // grpId
    std::string tempId;
    c.getString(mColGrpMeta_GrpId + colOffset, tempId);

    std::shared_ptr<RsGxsGrpMetaData> grpMeta ;
	RsGxsGroupId grpId(tempId) ;

    if(grpId.isNull())			// not in the DB!
        return nullptr;

    if(mUseCache)
        grpMeta = mGrpMetaDataCache.getOrCreateMeta(grpId);
	else
        grpMeta = std::make_shared<RsGxsGrpMetaData>();

    if(!grpMeta->mGroupId.isNull())	// the grpMeta is already initialized because it comes from the cache
        return grpMeta;

    grpMeta->mGroupId = RsGxsGroupId(tempId);
    c.getString(mColGrpMeta_NxsIdentity + colOffset, tempId);
    grpMeta->mAuthorId = RsGxsId(tempId);

    c.getString(mColGrpMeta_Name + colOffset, grpMeta->mGroupName);
    c.getString(mColGrpMeta_OrigGrpId + colOffset, tempId);
    grpMeta->mOrigGrpId = RsGxsGroupId(tempId);
    c.getString(mColGrpMeta_ServString + colOffset, grpMeta->mServiceString);
    std::string temp;
    c.getString(mColGrpMeta_NxsHash + colOffset, temp);
    grpMeta->mHash = RsFileHash(temp);
    grpMeta->mReputationCutOff = c.getInt32(mColGrpMeta_RepCutoff + colOffset);
    grpMeta->mSignFlags = c.getInt32(mColGrpMeta_SignFlags + colOffset);

    grpMeta->mPublishTs = c.getInt32(mColGrpMeta_TimeStamp + colOffset);
    grpMeta->mGroupFlags = c.getInt32(mColGrpMeta_NxsFlags + colOffset);
    grpMeta->mGrpSize = c.getInt32(mColGrpMeta_NxsDataLen + colOffset);

    offset = 0; data = NULL; data_len = 0;
    data = (char*)c.getData(mColGrpMeta_KeySet + colOffset, data_len);

    if(data)
        ok &= grpMeta->keys.GetTlv(data, data_len, &offset);
     else
         grpMeta->keys.TlvClear() ;

    // local meta
    grpMeta->mSubscribeFlags = c.getInt32(mColGrpMeta_SubscrFlag + colOffset);
    grpMeta->mPop = c.getInt32(mColGrpMeta_Pop + colOffset);
    grpMeta->mVisibleMsgCount = c.getInt32(mColGrpMeta_MsgCount + colOffset);
    grpMeta->mLastPost = c.getInt32(mColGrpMeta_LastPost + colOffset);
    grpMeta->mGroupStatus = c.getInt32(mColGrpMeta_Status + colOffset);

    c.getString(mColGrpMeta_CircleId + colOffset, tempId);
    grpMeta->mCircleId = RsGxsCircleId(tempId);
    grpMeta->mCircleType = c.getInt32(mColGrpMeta_CircleType + colOffset);
    c.getString(mColGrpMeta_InternCircle + colOffset, tempId);
    grpMeta->mInternalCircle = RsGxsCircleId(tempId);

    std::string s ; c.getString(mColGrpMeta_Originator + colOffset, s) ;
    grpMeta->mOriginator = RsPeerId(s);
    grpMeta->mAuthenFlags = c.getInt32(mColGrpMeta_AuthenFlags + colOffset);
    grpMeta->mRecvTS = c.getInt32(mColGrpMeta_RecvTs + colOffset);


    c.getString(mColGrpMeta_ParentGrpId, tempId);
    grpMeta->mParentGrpId = RsGxsGroupId(tempId);

	// make sure that flags and keys are actually consistent

	bool have_private_admin_key = false ;
	bool have_private_publish_key = false ;

	for(auto mit = grpMeta->keys.private_keys.begin(); mit != grpMeta->keys.private_keys.end();++mit)
	{
		if(mit->second.keyFlags == (RSTLV_KEY_DISTRIB_PUBLISH | RSTLV_KEY_TYPE_FULL)) have_private_publish_key = true ;
		if(mit->second.keyFlags == (RSTLV_KEY_DISTRIB_ADMIN   | RSTLV_KEY_TYPE_FULL)) have_private_admin_key = true ;
	}

	if(have_private_admin_key && !(grpMeta->mSubscribeFlags & GXS_SERV::GROUP_SUBSCRIBE_ADMIN))
	{
		std::cerr << "(WW) inconsistency in group " << grpMeta->mGroupId << ": group does not have flag ADMIN but an admin key was found. Updating the flags." << std::endl;
		grpMeta->mSubscribeFlags |= GXS_SERV::GROUP_SUBSCRIBE_ADMIN;
	}
	if(!have_private_admin_key && (grpMeta->mSubscribeFlags & GXS_SERV::GROUP_SUBSCRIBE_ADMIN))
	{
		std::cerr << "(WW) inconsistency in group " << grpMeta->mGroupId << ": group has flag ADMIN but no admin key found. Updating the flags." << std::endl;
		grpMeta->mSubscribeFlags &= ~GXS_SERV::GROUP_SUBSCRIBE_ADMIN;
	}
	if(have_private_publish_key && !(grpMeta->mSubscribeFlags & GXS_SERV::GROUP_SUBSCRIBE_PUBLISH))
	{
		std::cerr << "(WW) inconsistency in group " << grpMeta->mGroupId << ": group does not have flag PUBLISH but an admin key was found. Updating the flags." << std::endl;
		grpMeta->mSubscribeFlags |= GXS_SERV::GROUP_SUBSCRIBE_PUBLISH;
	}
	if(!have_private_publish_key && (grpMeta->mSubscribeFlags & GXS_SERV::GROUP_SUBSCRIBE_PUBLISH))
	{
		std::cerr << "(WW) inconsistency in group " << grpMeta->mGroupId << ": group has flag PUBLISH but no admin key found. Updating the flags." << std::endl;
		grpMeta->mSubscribeFlags &= ~GXS_SERV::GROUP_SUBSCRIBE_PUBLISH;
	}

    if(ok)
        return grpMeta;
    else
		return NULL;
}

RsNxsGrp* RsDataService::locked_getGroup(RetroCursor &c)
{
    /*!
     * grpId, pub admin and pub publish key
     * necessary for successful group
     */
    RsNxsGrp* grp = new RsNxsGrp(mServType);
    bool ok = true;

    // for manipulating raw data
    uint32_t offset = 0;
    char* data = NULL;
    uint32_t data_len = 0;

    // grpId
    c.getStringT<RsGxsGroupId>(mColGrp_GrpId, grp->grpId);
    ok &= !grp->grpId.isNull();

    offset = 0; data_len = 0;
    if(ok){

        data = (char*)c.getData(mColGrp_MetaData, data_len);
        if(data)
            grp->meta.GetTlv(data, data_len, &offset);
    }

    /* now retrieve grp data */
    offset = 0; data_len = 0;
    if(ok){
        data = (char*)c.getData(mColGrp_NxsData, data_len);
        if(data)
            ok &= grp->grp.GetTlv(data, data_len, &offset);
    }

    if(ok)
        return grp;
    else
        delete grp;

    return NULL;
}

std::shared_ptr<RsGxsMsgMetaData> RsDataService::locked_getMsgMeta(RetroCursor &c, int colOffset)
{
    bool ok = true;
    uint32_t data_len = 0,
    offset = 0;
    char* data = NULL;

    RsGxsGroupId group_id;
    RsGxsMessageId msg_id;

    std::string gId;
    c.getString(mColMsgMeta_GrpId + colOffset, gId);
    group_id = RsGxsGroupId(gId);
    std::string temp;
    c.getString(mColMsgMeta_MsgId + colOffset, temp);
    msg_id = RsGxsMessageId(temp);

    // without these, a msg is meaningless
    if(group_id.isNull() || msg_id.isNull())
        return nullptr;

    std::shared_ptr<RsGxsMsgMetaData> msgMeta;

    if(mUseCache)
        msgMeta = mMsgMetaDataCache[group_id].getOrCreateMeta(msg_id);
	else
        msgMeta = std::make_shared<RsGxsMsgMetaData>();

    if(!msgMeta->mGroupId.isNull())	// we cannot do that because the cursor needs to advance. Is there a method to skip some data in the db?
        return msgMeta;

	msgMeta->mGroupId = group_id;
	msgMeta->mMsgId = msg_id;

    c.getString(mColMsgMeta_OrigMsgId + colOffset, temp);
    msgMeta->mOrigMsgId = RsGxsMessageId(temp);
    c.getString(mColMsgMeta_NxsIdentity + colOffset, temp);
    msgMeta->mAuthorId = RsGxsId(temp);
    c.getString(mColMsgMeta_Name + colOffset, msgMeta->mMsgName);
    c.getString(mColMsgMeta_NxsServString + colOffset, msgMeta->mServiceString);

    c.getString(mColMsgMeta_NxsHash + colOffset, temp);
    msgMeta->mHash = RsFileHash(temp);
    msgMeta->recvTS = c.getInt32(mColMsgMeta_RecvTs + colOffset);
    offset = 0;
    data = (char*)c.getData(mColMsgMeta_SignSet + colOffset, data_len);
    msgMeta->signSet.GetTlv(data, data_len, &offset);
    msgMeta->mMsgSize = c.getInt32(mColMsgMeta_NxsDataLen + colOffset);

    msgMeta->mMsgFlags = c.getInt32(mColMsgMeta_NxsFlags + colOffset);
    msgMeta->mPublishTs = c.getInt32(mColMsgMeta_TimeStamp + colOffset);

    offset = 0; data_len = 0;

    // thread and parent id
    c.getString(mColMsgMeta_MsgThreadId + colOffset, temp);
    msgMeta->mThreadId = RsGxsMessageId(temp);
    c.getString(mColMsgMeta_MsgParentId + colOffset, temp);
    msgMeta->mParentId = RsGxsMessageId(temp);

    // local meta
    msgMeta->mMsgStatus = c.getInt32(mColMsgMeta_MsgStatus + colOffset);
    msgMeta->mChildTs = c.getInt32(mColMsgMeta_ChildTs + colOffset);

    if(ok)
        return msgMeta;

    return nullptr;
}



RsNxsMsg* RsDataService::locked_getMessage(RetroCursor &c)
{

    RsNxsMsg* msg = new RsNxsMsg(mServType);

    bool ok = true;
    uint32_t data_len = 0,
    offset = 0;
    char* data = NULL;
    c.getStringT<RsGxsGroupId>(mColMsg_GrpId, msg->grpId);
    std::string temp;
    c.getString(mColMsg_MsgId, temp);
    msg->msgId = RsGxsMessageId(temp);

    ok &= (!msg->grpId.isNull()) && (!msg->msgId.isNull());

    offset = 0; data_len = 0;
    if(ok){

        data = (char*)c.getData(mColMsg_MetaData, data_len);
        if(data)
            msg->meta.GetTlv(data, data_len, &offset);
    }

    /* now retrieve msg data */
    offset = 0; data_len = 0;
    if(ok){
        data = (char*)c.getData(mColMsg_NxsData, data_len);
        if(data)
            ok &= msg->msg.GetTlv(data, data_len, &offset);
    }

    if(ok)
        return msg;

    delete msg;
    return nullptr;
}

int RsDataService::storeMessage(const std::list<RsNxsMsg*>& msg)
{

    RsStackMutex stack(mDbMutex);

    // start a transaction
    mDb->beginTransaction();

    for(std::list<RsNxsMsg*>::const_iterator mit = msg.begin(); mit != msg.end(); ++mit)
    {
        RsNxsMsg* msgPtr = *mit;
        RsGxsMsgMetaData* msgMetaPtr = msgPtr->metaData;

		assert(msgMetaPtr != NULL);

#ifdef RS_DATA_SERVICE_DEBUG
        std::cerr << "RsDataService::storeMessage() ";
        std::cerr << " GroupId: " << msgMetaPtr->mGroupId.toStdString();
        std::cerr << " MessageId: " << msgMetaPtr->mMsgId.toStdString();
        std::cerr << std::endl;
#endif

        // skip msg item if size if greater than
        if(!validSize(msgPtr))
        {
            std::cerr << "RsDataService::storeMessage() ERROR invalid size";
            std::cerr << std::endl;
            continue;
        }

        ContentValue cv;

        uint32_t dataLen = msgPtr->msg.TlvSize();
        char msgData[dataLen];
        uint32_t offset = 0;
        msgPtr->msg.SetTlv(msgData, dataLen, &offset);
        cv.put(KEY_NXS_DATA, dataLen, msgData);

        cv.put(KEY_NXS_DATA_LEN, (int32_t)dataLen);
        cv.put(KEY_MSG_ID, msgMetaPtr->mMsgId.toStdString());
        cv.put(KEY_GRP_ID, msgMetaPtr->mGroupId.toStdString());
        cv.put(KEY_NXS_SERV_STRING, msgMetaPtr->mServiceString);
        cv.put(KEY_NXS_HASH, msgMetaPtr->mHash.toStdString());
        cv.put(KEY_RECV_TS, (int32_t)msgMetaPtr->recvTS);


        char signSetData[msgMetaPtr->signSet.TlvSize()];
        offset = 0;
        msgMetaPtr->signSet.SetTlv(signSetData, msgMetaPtr->signSet.TlvSize(), &offset);
        cv.put(KEY_SIGN_SET, msgMetaPtr->signSet.TlvSize(), signSetData);
        cv.put(KEY_NXS_IDENTITY, msgMetaPtr->mAuthorId.toStdString());


        cv.put(KEY_NXS_FLAGS, (int32_t) msgMetaPtr->mMsgFlags);
        cv.put(KEY_TIME_STAMP, (int32_t) msgMetaPtr->mPublishTs);

        offset = 0;
        char metaData[msgPtr->meta.TlvSize()];
        msgPtr->meta.SetTlv(metaData, msgPtr->meta.TlvSize(), &offset);
        cv.put(KEY_NXS_META, msgPtr->meta.TlvSize(), metaData);

        cv.put(KEY_MSG_PARENT_ID, msgMetaPtr->mParentId.toStdString());
        cv.put(KEY_MSG_THREAD_ID, msgMetaPtr->mThreadId.toStdString());
        cv.put(KEY_ORIG_MSG_ID, msgMetaPtr->mOrigMsgId.toStdString());
        cv.put(KEY_MSG_NAME, msgMetaPtr->mMsgName);

        // now local meta
        cv.put(KEY_MSG_STATUS, (int32_t)msgMetaPtr->mMsgStatus);
        cv.put(KEY_CHILD_TS, (int32_t)msgMetaPtr->mChildTs);

        if (!mDb->sqlInsert(MSG_TABLE_NAME, "", cv))
        {
            std::cerr << "RsDataService::storeMessage() sqlInsert Failed";
            std::cerr << std::endl;
            std::cerr << "\t For GroupId: " << msgMetaPtr->mGroupId.toStdString();
            std::cerr << std::endl;
            std::cerr << "\t & MessageId: " << msgMetaPtr->mMsgId.toStdString();
            std::cerr << std::endl;
        }

        // This is needed so that mLastPost is correctly updated in the group meta when it is re-loaded.

        if(mUseCache)
                mMsgMetaDataCache[msgMetaPtr->mGroupId].updateMeta(msgMetaPtr->mMsgId,*msgMetaPtr);

        delete *mit;
    }

    // finish transaction
    bool ret = mDb->commitTransaction();

    return ret;
}

bool RsDataService::validSize(RsNxsMsg* msg) const
{
    if((msg->msg.TlvSize() + msg->meta.TlvSize()) <= GXS_MAX_ITEM_SIZE) return true;

    return false;
}


int RsDataService::storeGroup(const std::list<RsNxsGrp*>& grp)
{

    RsStackMutex stack(mDbMutex);

    // begin transaction
    mDb->beginTransaction();

    for(std::list<RsNxsGrp*>::const_iterator sit = grp.begin();sit != grp.end(); ++sit)
	{
		RsNxsGrp* grpPtr = *sit;
		RsGxsGrpMetaData* grpMetaPtr = grpPtr->metaData;

		assert(grpMetaPtr != NULL);

		// if data is larger than max item size do not add
		if(!validSize(grpPtr)) continue;

#ifdef RS_DATA_SERVICE_DEBUG
		std::cerr << "RsDataService::storeGroup() GrpId: " << grpPtr->grpId.toStdString();
		std::cerr << " CircleType: " << (uint32_t) grpMetaPtr->mCircleType;
		std::cerr << " CircleId: " << grpMetaPtr->mCircleId.toStdString();
		std::cerr << std::endl;
#endif

		/*!
		 * STORE data, data len,
		 * grpId, flags, publish time stamp, identity,
		 * id signature, admin signatue, key set, last posting ts
		 * and meta data
		 **/
		ContentValue cv;

		uint32_t dataLen = grpPtr->grp.TlvSize();
		char grpData[dataLen];
		uint32_t offset = 0;
		grpPtr->grp.SetTlv(grpData, dataLen, &offset);
		cv.put(KEY_NXS_DATA, dataLen, grpData);

		cv.put(KEY_NXS_DATA_LEN, (int32_t) dataLen);
		cv.put(KEY_GRP_ID, grpPtr->grpId.toStdString());
		cv.put(KEY_GRP_NAME, grpMetaPtr->mGroupName);
		cv.put(KEY_ORIG_GRP_ID, grpMetaPtr->mOrigGrpId.toStdString());
		cv.put(KEY_NXS_SERV_STRING, grpMetaPtr->mServiceString);
		cv.put(KEY_NXS_FLAGS, (int32_t)grpMetaPtr->mGroupFlags);
		cv.put(KEY_TIME_STAMP, (int32_t)grpMetaPtr->mPublishTs);
		cv.put(KEY_GRP_SIGN_FLAGS, (int32_t)grpMetaPtr->mSignFlags);
		cv.put(KEY_GRP_CIRCLE_ID, grpMetaPtr->mCircleId.toStdString());
		cv.put(KEY_GRP_CIRCLE_TYPE, (int32_t)grpMetaPtr->mCircleType);
		cv.put(KEY_GRP_INTERNAL_CIRCLE, grpMetaPtr->mInternalCircle.toStdString());
		cv.put(KEY_GRP_ORIGINATOR, grpMetaPtr->mOriginator.toStdString());
		cv.put(KEY_GRP_AUTHEN_FLAGS, (int32_t)grpMetaPtr->mAuthenFlags);
		cv.put(KEY_PARENT_GRP_ID, grpMetaPtr->mParentGrpId.toStdString());
		cv.put(KEY_NXS_HASH, grpMetaPtr->mHash.toStdString());
		cv.put(KEY_RECV_TS, (int32_t)grpMetaPtr->mRecvTS);
		cv.put(KEY_GRP_REP_CUTOFF, (int32_t)grpMetaPtr->mReputationCutOff);
		cv.put(KEY_NXS_IDENTITY, grpMetaPtr->mAuthorId.toStdString());

		offset = 0;
		char keySetData[grpMetaPtr->keys.TlvSize()];
		grpMetaPtr->keys.SetTlv(keySetData, grpMetaPtr->keys.TlvSize(), &offset);
		cv.put(KEY_KEY_SET, grpMetaPtr->keys.TlvSize(), keySetData);

		offset = 0;
		char metaData[grpPtr->meta.TlvSize()];
		grpPtr->meta.SetTlv(metaData, grpPtr->meta.TlvSize(), &offset);
		cv.put(KEY_NXS_META, grpPtr->meta.TlvSize(), metaData);

		// local meta data
		cv.put(KEY_GRP_SUBCR_FLAG, (int32_t)grpMetaPtr->mSubscribeFlags);
		cv.put(KEY_GRP_POP, (int32_t)grpMetaPtr->mPop);
		cv.put(KEY_MSG_COUNT, (int32_t)grpMetaPtr->mVisibleMsgCount);
		cv.put(KEY_GRP_STATUS, (int32_t)grpMetaPtr->mGroupStatus);
		cv.put(KEY_GRP_LAST_POST, (int32_t)grpMetaPtr->mLastPost);

		mGrpMetaDataCache.updateMeta(grpMetaPtr->mGroupId,*grpMetaPtr);

		if (!mDb->sqlInsert(GRP_TABLE_NAME, "", cv))
		{
			std::cerr << "RsDataService::storeGroup() sqlInsert Failed";
			std::cerr << std::endl;
			std::cerr << "\t For GroupId: " << grpMetaPtr->mGroupId.toStdString();
			std::cerr << std::endl;
		}

        delete *sit;
	}
    // finish transaction
    bool ret = mDb->commitTransaction();

    return ret;
}

int RsDataService::updateGroup(const std::list<RsNxsGrp *> &grp)
{

    RsStackMutex stack(mDbMutex);

    // begin transaction
    mDb->beginTransaction();

    for( std::list<RsNxsGrp*>::const_iterator sit = grp.begin(); sit != grp.end(); ++sit)
    {

        RsNxsGrp* grpPtr = *sit;
        RsGxsGrpMetaData* grpMetaPtr = grpPtr->metaData;

		assert(grpMetaPtr != NULL);

        // if data is larger than max item size do not add
        if(!validSize(grpPtr)) continue;

        /*!
         * STORE data, data len,
         * grpId, flags, publish time stamp, identity,
         * id signature, admin signatue, key set, last posting ts
         * and meta data
         **/
        ContentValue cv;
        uint32_t dataLen = grpPtr->grp.TlvSize();
        char grpData[dataLen];
        uint32_t offset = 0;
        grpPtr->grp.SetTlv(grpData, dataLen, &offset);
        cv.put(KEY_NXS_DATA, dataLen, grpData);

        cv.put(KEY_NXS_DATA_LEN, (int32_t) dataLen);
        cv.put(KEY_GRP_ID, grpPtr->grpId.toStdString());
        cv.put(KEY_GRP_NAME, grpMetaPtr->mGroupName);
        cv.put(KEY_ORIG_GRP_ID, grpMetaPtr->mOrigGrpId.toStdString());
        cv.put(KEY_NXS_SERV_STRING, grpMetaPtr->mServiceString);
        cv.put(KEY_NXS_FLAGS, (int32_t)grpMetaPtr->mGroupFlags);
        cv.put(KEY_TIME_STAMP, (int32_t)grpMetaPtr->mPublishTs);
        cv.put(KEY_GRP_SIGN_FLAGS, (int32_t)grpMetaPtr->mSignFlags);
        cv.put(KEY_GRP_CIRCLE_ID, grpMetaPtr->mCircleId.toStdString());
        cv.put(KEY_GRP_CIRCLE_TYPE, (int32_t)grpMetaPtr->mCircleType);
        cv.put(KEY_GRP_INTERNAL_CIRCLE, grpMetaPtr->mInternalCircle.toStdString());
        cv.put(KEY_GRP_ORIGINATOR, grpMetaPtr->mOriginator.toStdString());
        cv.put(KEY_GRP_AUTHEN_FLAGS, (int32_t)grpMetaPtr->mAuthenFlags);
        cv.put(KEY_NXS_HASH, grpMetaPtr->mHash.toStdString());
        cv.put(KEY_RECV_TS, (int32_t)grpMetaPtr->mRecvTS);
        cv.put(KEY_NXS_IDENTITY, grpMetaPtr->mAuthorId.toStdString());

        offset = 0;
        char keySetData[grpMetaPtr->keys.TlvSize()];
        grpMetaPtr->keys.SetTlv(keySetData, grpMetaPtr->keys.TlvSize(), &offset);
        cv.put(KEY_KEY_SET, grpMetaPtr->keys.TlvSize(), keySetData);

        offset = 0;
        char metaData[grpPtr->meta.TlvSize()];
        grpPtr->meta.SetTlv(metaData, grpPtr->meta.TlvSize(), &offset);
        cv.put(KEY_NXS_META, grpPtr->meta.TlvSize(), metaData);

        // local meta data
        cv.put(KEY_GRP_SUBCR_FLAG, (int32_t)grpMetaPtr->mSubscribeFlags);
        cv.put(KEY_GRP_POP, (int32_t)grpMetaPtr->mPop);
        cv.put(KEY_MSG_COUNT, (int32_t)grpMetaPtr->mVisibleMsgCount);
        cv.put(KEY_GRP_STATUS, (int32_t)grpMetaPtr->mGroupStatus);
        cv.put(KEY_GRP_LAST_POST, (int32_t)grpMetaPtr->mLastPost);

        mDb->sqlUpdate(GRP_TABLE_NAME, "grpId='" + grpPtr->grpId.toStdString() + "'", cv);

        mGrpMetaDataCache.updateMeta(grpMetaPtr->mGroupId,*grpMetaPtr);

        delete *sit;
    }
    // finish transaction
    bool ret = mDb->commitTransaction();

    return ret;
}

int RsDataService::updateGroupKeys(const RsGxsGroupId& grpId,const RsTlvSecurityKeySet& keys,uint32_t subscribe_flags)
{
    RsStackMutex stack(mDbMutex);

    // begin transaction
    mDb->beginTransaction();

    /*!
     * STORE key set
     **/

    ContentValue cv;
        //cv.put(KEY_NXS_FLAGS, (int32_t)grpMetaPtr->mGroupFlags); ?

    uint32_t offset = 0;
    char keySetData[keys.TlvSize()];
    keys.SetTlv(keySetData, keys.TlvSize(), &offset);
    cv.put(KEY_KEY_SET, keys.TlvSize(), keySetData);
    cv.put(KEY_GRP_SUBCR_FLAG, (int32_t)subscribe_flags);

    mDb->sqlUpdate(GRP_TABLE_NAME, "grpId='" + grpId.toStdString() + "'", cv);

    // finish transaction
    bool res = mDb->commitTransaction();

    mGrpMetaDataCache.clear(grpId);
    mGrpMetaDataCache.setCacheUpToDate(false);	// this is needed because clear() doesn't do it (on purpose)

    return res;
}

bool RsDataService::validSize(RsNxsGrp* grp) const
{
    if((grp->grp.TlvSize() + grp->meta.TlvSize()) <= GXS_MAX_ITEM_SIZE) return true;
    return false;
}

int RsDataService::retrieveNxsGrps(std::map<RsGxsGroupId, RsNxsGrp *> &grp, bool withMeta)
{
    RS_STACK_MUTEX(mDbMutex);

    if(grp.empty())
    {
        std::vector<RsNxsGrp*> grpSet;
        RetroCursor* c = mDb->sqlQuery(GRP_TABLE_NAME, withMeta ? mGrpColumnsWithMeta : mGrpColumns, "", "");
        if(c) { locked_retrieveGroups(c, grpSet, withMeta ? mColGrp_WithMetaOffset : 0);
            for(size_t i=0; i<grpSet.size(); ++i) if(grpSet[i]) grp[grpSet[i]->grpId] = grpSet[i];
            delete c;
        }
    }
    else
    {
        std::vector<RsGxsGroupId> ids;
        for(std::map<RsGxsGroupId, RsNxsGrp*>::iterator it = grp.begin(); it != grp.end(); ++it) ids.push_back(it->first);

        size_t idx = 0;
        while(idx < ids.size())
        {
            std::string idList;
            std::vector<RsGxsGroupId> currentBatch;
            for(int i = 0; i < 100 && idx < ids.size(); ++i, ++idx) {
                if(!idList.empty()) idList += ",";
                idList += "'" + ids[idx].toStdString() + "'";
                currentBatch.push_back(ids[idx]);
            }

            std::string selection = KEY_GRP_ID + " IN (" + idList + ")";
            std::vector<RsNxsGrp*> grpSet;
            RetroCursor* c = mDb->sqlQuery(GRP_TABLE_NAME, withMeta ? mGrpColumnsWithMeta : mGrpColumns, selection, "");
            
            if(c) {
                locked_retrieveGroups(c, grpSet, withMeta ? mColGrp_WithMetaOffset : 0);
                for(size_t i=0; i < grpSet.size(); ++i) if(grpSet[i]) grp[grpSet[i]->grpId] = grpSet[i];
                delete c;
            }

            // FALLBACK: Ensure every ID in this batch has a pointer in the map
            for(const auto& id : currentBatch) {
                if(grp[id] == NULL) {
                    std::vector<RsNxsGrp*> fallbackSet;
                    RetroCursor* cf = mDb->sqlQuery(GRP_TABLE_NAME, withMeta ? mGrpColumnsWithMeta : mGrpColumns, KEY_GRP_ID + "='" + id.toStdString() + "'", "");
                    if(cf) { locked_retrieveGroups(cf, fallbackSet, withMeta ? mColGrp_WithMetaOffset : 0);
                        if(!fallbackSet.empty()) grp[id] = fallbackSet[0];
                        delete cf;
                    }
                }
            }
        }
    }
    return 1;
}

void RsDataService::locked_retrieveGroups(RetroCursor* c, std::vector<RsNxsGrp*>& grps, int metaOffset)
{
    if(c){
        bool valid = c->moveToFirst();

        while(valid){
            RsNxsGrp* g = locked_getGroup(*c);

            // only add the latest grp info
            if(g)
            {
                if (metaOffset)
                    g->metaData = new RsGxsGrpMetaData(*locked_getGrpMeta(*c, metaOffset));
                else
                    g->metaData = nullptr;

                grps.push_back(g);
            }
            valid = c->moveToNext();
        }
    }
}

int RsDataService::retrieveNxsMsgs(const GxsMsgReq &reqIds, GxsMsgResult &msg, bool withMeta)
{
    auto start_total = std::chrono::steady_clock::now();
    RsDbg() << "DEBUG [DataService]: START retrieveNxsMsgs for " << reqIds.size() << " groups." << std::endl;

    for(GxsMsgReq::const_iterator mit = reqIds.begin(); mit != reqIds.end(); ++mit)
    {
        const RsGxsGroupId& grpId = mit->first;
        const std::set<RsGxsMessageId>& msgIdV = mit->second;
        std::vector<RsNxsMsg*>& finalSet = msg[grpId];

        RS_STACK_MUTEX(mDbMutex);
        auto start_loop = std::chrono::steady_clock::now();

        if(msgIdV.empty())
        {
            RetroCursor* c = mDb->sqlQuery(MSG_TABLE_NAME, withMeta ? mMsgColumnsWithMeta : mMsgColumns, 
                                          KEY_GRP_ID + "='" + grpId.toStdString() + "'", "");
            if(c) { locked_retrieveMessages(c, finalSet, withMeta ? mColMsg_WithMetaOffset : 0); delete c; }
        }
        else
        {
            std::set<RsGxsMessageId>::const_iterator sit = msgIdV.begin();
            while(sit != msgIdV.end())
            {
                std::string idList;
                std::vector<RsGxsMessageId> currentBatch;
                for(int i = 0; i < 100 && sit != msgIdV.end(); ++i, ++sit)
                {
                    if(!idList.empty()) idList += ",";
                    idList += "'" + sit->toStdString() + "'";
                    currentBatch.push_back(*sit);
                }

                auto start_sql = std::chrono::steady_clock::now();
                std::string selection = KEY_MSG_ID + " IN (" + idList + ")";
                RetroCursor* c = mDb->sqlQuery(MSG_TABLE_NAME, withMeta ? mMsgColumnsWithMeta : mMsgColumns, selection, "");
                
                if(c) { locked_retrieveMessages(c, finalSet, withMeta ? mColMsg_WithMetaOffset : 0); delete c; }

                auto end_sql = std::chrono::steady_clock::now();
                auto sql_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_sql - start_sql).count();
                RsDbg() << "DEBUG [DataService]: Batch SQL for group " << grpId << " took " << sql_ms << "ms." << std::endl;

                // FALLBACK Check
                if((finalSet.size()) < currentBatch.size())
                {
                    for(const auto& id : currentBatch)
                    {
                        bool found = false;
                        for(size_t j=0; j<finalSet.size(); ++j)
                            if(finalSet[j] && finalSet[j]->msgId == id) { found = true; break; }
                        
                        if(!found) {
                            RetroCursor* c2 = mDb->sqlQuery(MSG_TABLE_NAME, withMeta ? mMsgColumnsWithMeta : mMsgColumns, KEY_MSG_ID + "='" + id.toStdString() + "'", "");
                            if(c2) { locked_retrieveMessages(c2, finalSet, withMeta ? mColMsg_WithMetaOffset : 0); delete c2; }
                        }
                    }
                }
            }
        }
        auto end_loop = std::chrono::steady_clock::now();
        auto loop_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_loop - start_loop).count();
        RsDbg() << "DEBUG [DataService]: Group " << grpId << " (Total " << finalSet.size() << " msgs) processed in " << loop_ms << "ms." << std::endl;
    }

    auto end_total = std::chrono::steady_clock::now();
    auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_total - start_total).count();
    RsDbg() << "DEBUG [DataService]: END retrieveNxsMsgs total time: " << total_ms << "ms." << std::endl;
    return 1;
}

void RsDataService::locked_retrieveMessages(RetroCursor *c, std::vector<RsNxsMsg *> &msgs, int metaOffset)
{
    bool valid = c->moveToFirst();
    while(valid){
        RsNxsMsg* m = locked_getMessage(*c);

        if(m){
            if (metaOffset)
                m->metaData = new RsGxsMsgMetaData(*locked_getMsgMeta(*c, metaOffset));
            else
                m->metaData = nullptr;

            msgs.push_back(m);
        }

        valid = c->moveToNext();
    }
    return;
}

int RsDataService::retrieveGxsMsgMetaData(const GxsMsgReq& reqIds, GxsMsgMetaResult& msgMeta)
{
    RS_STACK_MUTEX(mDbMutex);

    for(GxsMsgReq::const_iterator mit = reqIds.begin(); mit != reqIds.end(); ++mit)
    {
        const RsGxsGroupId& grpId = mit->first;
        const std::set<RsGxsMessageId>& msgIdV = mit->second;
        std::vector<std::shared_ptr<RsGxsMsgMetaData>>& metaList = msgMeta[grpId];

        if(msgIdV.empty())
        {
            RetroCursor* c = mDb->sqlQuery(MSG_TABLE_NAME, mMsgMetaColumns, KEY_GRP_ID + "='" + grpId.toStdString() + "'", "");
            if (c) { locked_retrieveMsgMetaList(c, metaList); delete c; }
        }
        else
        {
            std::set<RsGxsMessageId>::const_iterator sit = msgIdV.begin();
            while(sit != msgIdV.end())
            {
                std::string idList;
                std::vector<RsGxsMessageId> currentBatch;
                for(int i = 0; i < 100 && sit != msgIdV.end(); ++i, ++sit)
                {
                    if(!idList.empty()) idList += ",";
                    idList += "'" + sit->toStdString() + "'";
                    currentBatch.push_back(*sit);
                }

                size_t sizeBefore = metaList.size();
                std::string selection = KEY_MSG_ID + " IN (" + idList + ")";
                RetroCursor* c = mDb->sqlQuery(MSG_TABLE_NAME, mMsgMetaColumns, selection, "");
                if(c) { locked_retrieveMsgMetaList(c, metaList); delete c; }

                // FALLBACK
                if((metaList.size() - sizeBefore) < currentBatch.size())
                {
                    for(const auto& id : currentBatch) {
                        bool found = false;
                        for(size_t j=sizeBefore; j<metaList.size(); ++j)
                            if(metaList[j]->mMsgId == id) { found = true; break; }
                        if(!found) {
                            RetroCursor* c2 = mDb->sqlQuery(MSG_TABLE_NAME, mMsgMetaColumns, KEY_MSG_ID + "='" + id.toStdString() + "'", "");
                            if(c2) { locked_retrieveMsgMetaList(c2, metaList); delete c2; }
                        }
                    }
                }
            }
        }
    }
    return 1;
}

void RsDataService::locked_retrieveGrpMetaList(RetroCursor *c, std::map<RsGxsGroupId,std::shared_ptr<RsGxsGrpMetaData> >& grpMeta)
{
	if(!c)
	{
        RsErr() << __PRETTY_FUNCTION__ << ": attempt to retrieve Group Meta data from the DB with null cursor!" << std::endl;
		return;
	}

	bool valid = c->moveToFirst();

	while(valid)
	{
        auto m = locked_getGrpMeta(*c, 0);

        if(m != nullptr)
			grpMeta[m->mGroupId] = m;

		valid = c->moveToNext();
	}
}

void RsDataService::locked_retrieveMsgMetaList(RetroCursor *c, std::vector<std::shared_ptr<RsGxsMsgMetaData> >& msgMeta)
{
	if(!c)
	{
		RsErr() << __PRETTY_FUNCTION__ << ": attempt to retrieve Msg Meta data from the DB with null cursor!" << std::endl;
		return;
	}

	bool valid = c->moveToFirst();
    while(valid)
    {
        auto m = locked_getMsgMeta(*c, 0);

        if(m != nullptr)
			msgMeta.push_back(m);

		valid = c->moveToNext();
	}
}

int RsDataService::retrieveGxsGrpMetaData(std::map<RsGxsGroupId, std::shared_ptr<RsGxsGrpMetaData> >& grp)
{
    RS_STACK_MUTEX(mDbMutex);

    if(grp.empty())
    {
        RetroCursor* c = mDb->sqlQuery(GRP_TABLE_NAME, mGrpMetaColumns, "", "");
        if(c)
        {
            locked_retrieveGrpMetaList(c, grp);
            delete c;
        }
    }
    else
    {
        std::vector<RsGxsGroupId> ids;
        for(std::map<RsGxsGroupId, std::shared_ptr<RsGxsGrpMetaData>>::iterator it = grp.begin(); it != grp.end(); ++it)
            ids.push_back(it->first);

        size_t idx = 0;
        while(idx < ids.size())
        {
            std::string idList;
            for(int i = 0; i < 200 && idx < ids.size(); ++i, ++idx)
            {
                if(!idList.empty()) idList += ",";
                idList += "'" + ids[idx].toStdString() + "'";
            }

            if(idList.empty()) continue;

            std::string selection = KEY_GRP_ID + " IN (" + idList + ")";
            RetroCursor* c = mDb->sqlQuery(GRP_TABLE_NAME, mGrpMetaColumns, selection, "");
            if(c)
            {
                locked_retrieveGrpMetaList(c, grp);
                delete c;
            }
        }
    }
    return 1;
}

int RsDataService::resetDataStore()
{

#ifdef RS_DATA_SERVICE_DEBUG
    std::cerr << "resetDataStore() " << std::endl;
#endif

    {
        RsStackMutex stack(mDbMutex);

        mDb->execSQL("DROP INDEX " + MSG_INDEX_GRPID);
        mDb->execSQL("DROP TABLE " + DATABASE_RELEASE_TABLE_NAME);
        mDb->execSQL("DROP TABLE " + MSG_TABLE_NAME);
        mDb->execSQL("DROP TABLE " + GRP_TABLE_NAME);
        mDb->execSQL("DROP TRIGGER " + GRP_LAST_POST_UPDATE_TRIGGER);
    }

    // recreate database
    initialise(true);

    return 1;
}

int RsDataService::updateGroupMetaData(const GrpLocMetaData& meta)
{
#ifdef RS_DATA_SERVICE_DEBUG_CACHE
    std::cerr << (void*)this << ": Updating Grp Meta data: grpId = " << meta.grpId << std::endl;
#endif

    RsStackMutex stack(mDbMutex);
    const RsGxsGroupId& grpId = meta.grpId;

#ifdef RS_DATA_SERVICE_DEBUG_CACHE
    std::cerr << (void*)this << ": erasing old entry from cache." << std::endl;
#endif

    if( mDb->sqlUpdate(GRP_TABLE_NAME,  KEY_GRP_ID+ "='" + grpId.toStdString() + "'", meta.val))
    {
        // If we use the cache, update the meta data immediately.

        if(mUseCache)
        {
            RetroCursor* c = mDb->sqlQuery(GRP_TABLE_NAME, mGrpMetaColumns, "grpId='" + grpId.toStdString() + "'", "");

            c->moveToFirst();

            // temporarily disable the cache so that we get the value from the DB itself.
            mUseCache=false;
            auto meta = locked_getGrpMeta(*c, 0);
            mUseCache=true;

            if(meta)
                mGrpMetaDataCache.updateMeta(grpId,meta);

            delete c;
        }

        return 1;
    }
    return 0;
}

int RsDataService::updateMessageMetaData(const MsgLocMetaData& metaData)
{
#ifdef RS_DATA_SERVICE_DEBUG_CACHE
    std::cerr << (void*)this << ": Updating Msg Meta data: grpId = " << metaData.msgId.first << " msgId = " << metaData.msgId.second << std::endl;
#endif

    RsStackMutex stack(mDbMutex);
    const RsGxsGroupId& grpId = metaData.msgId.first;
    const RsGxsMessageId& msgId = metaData.msgId.second;

    if(mDb->sqlUpdate(MSG_TABLE_NAME,  KEY_GRP_ID+ "='" + grpId.toStdString() + "' AND " + KEY_MSG_ID + "='" + msgId.toStdString() + "'", metaData.val) )
    {
        // If we use the cache, update the meta data immediately.

        if(mUseCache)
        {
            RetroCursor* c = mDb->sqlQuery(MSG_TABLE_NAME, mMsgMetaColumns, KEY_GRP_ID+ "='" + grpId.toStdString() + "' AND " + KEY_MSG_ID + "='" + msgId.toStdString() + "'", "");

            c->moveToFirst();

            // temporarily disable the cache so that we get the value from the DB itself.
            mUseCache=false;
            auto meta = locked_getMsgMeta(*c, 0);
            mUseCache=true;

            if(meta)
                mMsgMetaDataCache[grpId].updateMeta(msgId,meta);

            delete c;
        }

        return 1;
    }
    return 0;
}

// --- OPTIMIZATION START (Batch Update) ---
int RsDataService::updateMessageMetaData(const std::vector<MsgLocMetaData>& metaDataList)
{
    if(metaDataList.empty()) return 1;

#ifdef RS_DATA_SERVICE_DEBUG
    std::cerr << "RsDataService: Starting Batch Update for " << metaDataList.size() << " messages." << std::endl;
#endif

    RsStackMutex stack(mDbMutex);

    // 1. START GLOBAL TRANSACTION
    // Critical for performance: SQLite will perform a single fsync at the end.
    if (!mDb->beginTransaction())
    {
        std::cerr << "RsDataService::updateMessageMetaData(batch) failed to begin transaction." << std::endl;
        return 0;
    }

    bool globalSuccess = true;
    int successCount = 0;

    for(std::vector<MsgLocMetaData>::const_iterator it = metaDataList.begin(); it != metaDataList.end(); ++it)
    {
        const MsgLocMetaData& metaData = *it;
        const RsGxsGroupId& grpId = metaData.msgId.first;
        const RsGxsMessageId& msgId = metaData.msgId.second;

        // Perform SQL UPDATE (Fast because it's in-memory due to transaction)
        if(mDb->sqlUpdate(MSG_TABLE_NAME,  KEY_GRP_ID+ "='" + grpId.toStdString() + "' AND " + KEY_MSG_ID + "='" + msgId.toStdString() + "'", metaData.val) )
        {
            successCount++;
            
            // --- CACHE OPTIMIZATION (In-Memory Update) ---
            // Instead of re-reading DB (Slow), we update the object in memory if it exists.
            if(mUseCache)
            {
                // 1. Look up group cache first
                auto grpCacheIt = mMsgMetaDataCache.find(grpId);
                
                if (grpCacheIt != mMsgMetaDataCache.end())
                {
                    // 2. Look up message in this group's cache
                    std::shared_ptr<RsGxsMsgMetaData> cachedMeta = grpCacheIt->second.getMeta(msgId);
                    
                    if (cachedMeta)
                    {
                        // 3. HIT! Message is in memory (displayed). Update C++ object directly.
                        int32_t intVal;

                        // Update Status (Read/Unread/New...)
                        if(metaData.val.getAsInt32(KEY_MSG_STATUS, intVal))
                            cachedMeta->mMsgStatus = intVal;
                            
                        // Update Flags
                        if(metaData.val.getAsInt32(KEY_NXS_FLAGS, intVal))
                            cachedMeta->mMsgFlags = intVal;
                    }
                }
                // MISS: If message is not in cache, do nothing. 
                // Next DB read will fetch the updated version.
            }
            // --- END CACHE OPTIMIZATION ---
        }
        else
        {
            std::cerr << "RsDataService::updateMessageMetaData(batch) failed for msgId: " << msgId << std::endl;
            globalSuccess = false; 
        }
    }

    // 2. COMMIT TRANSACTION
    if (globalSuccess || successCount > 0)
    {
        mDb->commitTransaction();
    }
    else
    {
        mDb->rollbackTransaction();
    }

    return globalSuccess ? 1 : 0;
}
// --- OPTIMIZATION END ---

int RsDataService::removeMsgs(const GxsMsgReq& msgIds)
{
    RsStackMutex stack(mDbMutex);

    GxsMsgReq::const_iterator mit = msgIds.begin();

    for(; mit != msgIds.end(); ++mit)
    {
        const std::set<RsGxsMessageId>& msgIdV = mit->second;
        const RsGxsGroupId& grpId = mit->first;

        // delete messages
        GxsMsgReq msgsToDelete;
        msgsToDelete[grpId] = msgIdV;
        locked_removeMessageEntries(msgsToDelete);
    }

    return 1;
}

int RsDataService::removeGroups(const std::vector<RsGxsGroupId> &grpIds)
{

    RsStackMutex stack(mDbMutex);

    locked_removeGroupEntries(grpIds);

    return 1;
}

int RsDataService::retrieveGroupIds(std::vector<RsGxsGroupId> &grpIds)
{
    RsStackMutex stack(mDbMutex);

#ifdef RS_DATA_SERVICE_DEBUG_TIME
    rstime::RsScopeTimer timer("");
    int resultCount = 0;
#endif

    RetroCursor* c = mDb->sqlQuery(GRP_TABLE_NAME, mGrpIdColumn, "", "");

    if(c)
    {
        bool valid = c->moveToFirst();

        while(valid)
        {
            std::string grpId;
            c->getString(mColGrpId_GrpId, grpId);
            grpIds.push_back(RsGxsGroupId(grpId));
            valid = c->moveToNext();

#ifdef RS_DATA_SERVICE_DEBUG_TIME
            ++resultCount;
#endif
        }
        delete c;
    }else
    {
        return 0;
    }

#ifdef RS_DATA_SERVICE_DEBUG_TIME
    std::cerr << "RsDataService::retrieveGroupIds() " << mDbName << ", Results: " << resultCount << ", Time: " << timer.duration() << std::endl;
#endif

    return 1;
}

int RsDataService::retrieveMsgIds(const RsGxsGroupId& grpId, RsGxsMessageId::std_set& msgIds)
{
#ifdef RS_DATA_SERVICE_DEBUG_TIME
    rstime::RsScopeTimer timer("");
    int resultCount = 0;
#endif

    RetroCursor* c = mDb->sqlQuery(MSG_TABLE_NAME, mMsgIdColumn, KEY_GRP_ID+ "='" + grpId.toStdString() + "'", "");

    if(c)
    {
        bool valid = c->moveToFirst();

        while(valid)
        {
            std::string msgId;
            c->getString(mColMsgId_MsgId, msgId);

            if(c->columnCount() != 1)
            std::cerr << "(EE) ********* not retrieving all columns!!" << std::endl;

            msgIds.insert(RsGxsMessageId(msgId));
            valid = c->moveToNext();

#ifdef RS_DATA_SERVICE_DEBUG_TIME
            ++resultCount;
#endif
        }
        delete c;
    }else
    {
        return 0;
    }

#ifdef RS_DATA_SERVICE_DEBUG_TIME
    std::cerr << "RsDataService::retrieveNxsGrps() " << mDbName << ", Results: " << resultCount << ", Time: " << timer.duration() << std::endl;
#endif

    return 1;

}

bool RsDataService::locked_removeMessageEntries(const GxsMsgReq& msgIds)
{
    // start a transaction
    bool ret = mDb->beginTransaction();

    GxsMsgReq::const_iterator mit = msgIds.begin();

    for(; mit != msgIds.end(); ++mit)
    {
        const RsGxsGroupId& grpId = mit->first;
        const std::set<RsGxsMessageId>& msgsV = mit->second;
        auto& cache(mMsgMetaDataCache[grpId]);

        for(auto& msgId:msgsV)
        {
            mDb->sqlDelete(MSG_TABLE_NAME, KEY_GRP_ID+ "='" + grpId.toStdString() + "' AND " + KEY_MSG_ID + "='" + msgId.toStdString() + "'", "");

            cache.clear(msgId);
        }
    }

    ret &= mDb->commitTransaction();

    return ret;
}

bool RsDataService::locked_removeGroupEntries(const std::vector<RsGxsGroupId>& grpIds)
{
    // start a transaction
    bool ret = mDb->beginTransaction();

    for(auto grpId:grpIds)
    {
        mDb->sqlDelete(GRP_TABLE_NAME, KEY_GRP_ID+ "='" + grpId.toStdString() + "'", "");

		// also remove the group meta from cache.
		mGrpMetaDataCache.clear(grpId) ;
    }

    ret &= mDb->commitTransaction();
    return ret;
}

uint32_t RsDataService::cacheSize() const {
    return 0;
}

int RsDataService::setCacheSize(uint32_t /* size */)
{
    return 0;
}

void RsDataService::debug_printCacheSize()
{
    RS_STACK_MUTEX(mDbMutex);

    uint32_t nb_items;
    uint64_t total_size;

    mGrpMetaDataCache.debug_computeSize(nb_items, total_size);

    RsDbg() << "[CACHE] Cache size: " << std::endl;
    RsDbg() << "[CACHE]    Groups: " << " total: " << nb_items << ", size: " << total_size << std::endl;

    nb_items = 0;
    total_size = 0;

    for(auto& it:mMsgMetaDataCache)
    {
        uint32_t tmp_nb_items;
        uint64_t tmp_total_size;

        it.second.debug_computeSize(tmp_nb_items, tmp_total_size);

        nb_items += tmp_nb_items;
        total_size += tmp_total_size;
    }
    RsDbg() << "[CACHE]    Msgs:   " << " total: " << nb_items << ", size: " << total_size << std::endl;
}









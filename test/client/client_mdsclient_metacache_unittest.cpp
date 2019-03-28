/*
 * Project: curve
 * File Created: Tuesday, 9th October 2018 5:16:52 pm
 * Author: tongguangxun
 * Copyright (c) 2018 NetEase
 */

#include <gtest/gtest.h>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <brpc/controller.h>
#include <brpc/server.h>

#include <string>
#include <thread>   //NOLINT
#include <chrono>   //NOLINT
#include <vector>

#include "src/client/client_common.h"
#include "src/client/file_instance.h"
#include "test/client/fake/mockMDS.h"
#include "src/client/metacache.h"
#include "test/client/fake/mock_schedule.h"
#include "include/client/libcurve_qemu.h"
#include "src/client/libcurve_file.h"
#include "src/client/client_config.h"
#include "src/client/service_helper.h"
#include "src/client/mds_client.h"
#include "src/client/config_info.h"
#include "test/client/fake/fakeMDS.h"
#include "src/client/metacache_struct.h"

extern std::string metaserver_addr;
extern uint32_t chunk_size;
extern std::string configpath;

using curve::client::CopysetInfo_t;
using curve::client::SegmentInfo;
using curve::client::FInfo;
using curve::client::LeaseSession;
using curve::client::UserInfo;
using curve::client::LogicalPoolCopysetIDInfo_t;
using curve::client::MetaCacheErrorType;
using curve::client::MDSClient;
using curve::client::ServiceHelper;
using curve::client::FileClient;
using curve::client::LogicPoolID;
using curve::client::CopysetID;
using curve::client::Configuration;
using curve::client::ChunkServerID;
using curve::client::PeerId;
using curve::client::FileInstance;
using curve::mds::CurveFSService;
using curve::mds::topology::TopologyService;
using ::curve::mds::topology::GetChunkServerListInCopySetsResponse;

class MDSClientTest : public ::testing::Test {
 public:
    void SetUp() {
        metaopt.metaaddrvec.push_back("127.0.0.1:8000");

        metaopt.metaaddrvec.push_back("127.0.0.1:8000");
        metaopt.rpcTimeoutMs = 500;
        metaopt.rpcRetryTimes = 5;
        metaopt.retryIntervalUs = 200;
        mdsclient_.Initialize(metaopt);
    }

    void TearDown() {
        mdsclient_.UnInitialize();
    }

    MDSClient           mdsclient_;
    MetaServerOption_t  metaopt;
    static int i;
};

TEST_F(MDSClientTest, Createfile) {
    std::string filename = "/1_userinfo_";
    size_t len = 4 * 1024 * 1024;

    brpc::Server server;

    FakeCurveFSService curvefsservice;

    if (server.AddService(&curvefsservice,
                          brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(FATAL) << "Fail to add service";
    }

    brpc::ServerOptions options;
    options.idle_timeout_sec = -1;
    LOG(INFO) << "meta server addr = " << metaserver_addr.c_str();
    ASSERT_EQ(server.Start(metaserver_addr.c_str(), &options), 0);

    // set response file exist
    ::curve::mds::CreateFileResponse response;
    response.set_statuscode(::curve::mds::StatusCode::kFileExists);

    FakeReturn* fakeret
     = new FakeReturn(nullptr, static_cast<void*>(&response));

    curvefsservice.SetFakeReturn(fakeret);

    LOG(INFO) << "now create file!";
    LIBCURVE_ERROR ret = mdsclient_.CreateFile(filename.c_str(),
                                    UserInfo("test", ""), len);
    ASSERT_EQ(ret, LIBCURVE_ERROR::EXISTS);

    // set response file exist
    ::curve::mds::CreateFileResponse response1;
    response1.set_statuscode(::curve::mds::StatusCode::kOK);

    FakeReturn* fakeret1
     = new FakeReturn(nullptr, static_cast<void*>(&response1));

    curvefsservice.SetFakeReturn(fakeret1);
    ASSERT_EQ(LIBCURVE_ERROR::OK, mdsclient_.CreateFile(filename.c_str(),
                                    UserInfo("test", ""), len));


    // 设置rpc失败，触发重试
    brpc::Controller cntl;
    cntl.SetFailed(-1, "failed");

    FakeReturn* fakeret2
     = new FakeReturn(&cntl, static_cast<void*>(&response));

    curvefsservice.SetFakeReturn(fakeret2);
    curvefsservice.CleanRetryTimes();

    ASSERT_EQ(LIBCURVE_ERROR::FAILED,
         mdsclient_.CreateFile(filename.c_str(),
                                UserInfo("test", ""), len));
    ASSERT_EQ(metaopt.rpcRetryTimes * metaopt.metaaddrvec.size(),
        curvefsservice.GetRetryTimes());

    LOG(INFO) << "create file done!";
    ASSERT_EQ(0, server.Stop(0));
    ASSERT_EQ(0, server.Join());
    delete fakeret;
    delete fakeret2;
}

TEST_F(MDSClientTest, Closefile) {
    std::string filename = "/1_userinfo_";
    size_t len = 4 * 1024 * 1024;

    brpc::Server server;

    FakeMDSCurveFSService curvefsservice;

    if (server.AddService(&curvefsservice,
                          brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(FATAL) << "Fail to add service";
    }

    brpc::ServerOptions options;
    options.idle_timeout_sec = -1;
    LOG(INFO) << "meta server addr = " << metaserver_addr.c_str();
    ASSERT_EQ(server.Start(metaserver_addr.c_str(), &options), 0);

    // file not exist
    ::curve::mds::CloseFileResponse response;
    response.set_statuscode(::curve::mds::StatusCode::kFileNotExists);

    FakeReturn* fakeret
                    = new FakeReturn(nullptr, static_cast<void*>(&response));
    curvefsservice.SetCloseFile(fakeret);

    LOG(INFO) << "now create file!";
    LIBCURVE_ERROR ret = mdsclient_.CloseFile(filename.c_str(),
                            UserInfo("test", ""), "sessid");
    ASSERT_EQ(ret, LIBCURVE_ERROR::NOTEXIST);


    // file close ok
    ::curve::mds::CloseFileResponse response1;
    response1.set_statuscode(::curve::mds::StatusCode::kOK);

    FakeReturn* fakeret1
                = new FakeReturn(nullptr, static_cast<void*>(&response1));
    curvefsservice.SetCloseFile(fakeret1);

    LOG(INFO) << "now create file!";
    ret = mdsclient_.CloseFile(filename.c_str(),
                                UserInfo("test", ""), "sessid");
    ASSERT_EQ(ret, LIBCURVE_ERROR::OK);

    // 设置rpc失败，触发重试
    brpc::Controller cntl;
    cntl.SetFailed(-1, "failed");

    FakeReturn* fakeret2
                = new FakeReturn(&cntl, static_cast<void*>(&response));
    curvefsservice.SetCloseFile(fakeret2);
    curvefsservice.CleanRetryTimes();

    ASSERT_EQ(LIBCURVE_ERROR::FAILED,
         mdsclient_.CloseFile(filename.c_str(),
                                UserInfo("test", ""),  "sessid"));
    ASSERT_EQ(metaopt.rpcRetryTimes * metaopt.metaaddrvec.size(),
        curvefsservice.GetRetryTimes());

    LOG(INFO) << "create file done!";
    ASSERT_EQ(0, server.Stop(0));
    ASSERT_EQ(0, server.Join());
    delete fakeret;
    delete fakeret2;
}

TEST_F(MDSClientTest, Openfile) {
    std::string filename = "/1_userinfo_";
    size_t len = 4 * 1024 * 1024;

    brpc::Server server;

    FakeCurveFSService curvefsservice;

    if (server.AddService(&curvefsservice,
                          brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(FATAL) << "Fail to add service";
    }

    brpc::ServerOptions options;
    options.idle_timeout_sec = -1;
    LOG(INFO) << "meta server addr = " << metaserver_addr.c_str();
    ASSERT_EQ(server.Start(metaserver_addr.c_str(), &options), 0);

    /**
     * set openfile response
     */
    ::curve::mds::OpenFileResponse openresponse;

    openresponse.set_statuscode(::curve::mds::StatusCode::kOK);
    FakeReturn* fakeret
     = new FakeReturn(nullptr, static_cast<void*>(&openresponse));
    curvefsservice.SetOpenFile(fakeret);

    FInfo finfo;
    LeaseSession lease;
    ASSERT_EQ(mdsclient_.OpenFile(filename, UserInfo("test", ""),
                 &finfo, &lease), LIBCURVE_ERROR::FAILED);

    // has protosession no fileinfo
    ::curve::mds::OpenFileResponse openresponse1;

    ::curve::mds::ProtoSession* se = new ::curve::mds::ProtoSession;
    se->set_sessionid("1");
    se->set_createtime(12345);
    se->set_leasetime(10000000);
    se->set_sessionstatus(::curve::mds::SessionStatus::kSessionOK);

    openresponse1.set_statuscode(::curve::mds::StatusCode::kOK);
    openresponse1.set_allocated_protosession(se);

    FakeReturn* fakeret1
     = new FakeReturn(nullptr, static_cast<void*>(&openresponse1));
    curvefsservice.SetOpenFile(fakeret1);

    ASSERT_EQ(mdsclient_.OpenFile(filename, UserInfo("test", ""),
                                &finfo, &lease), LIBCURVE_ERROR::FAILED);

    // has protosession and finfo
    ::curve::mds::OpenFileResponse openresponse2;

    ::curve::mds::ProtoSession* se2 = new ::curve::mds::ProtoSession;
    se2->set_sessionid("1");
    se2->set_createtime(12345);
    se2->set_leasetime(10000000);
    se2->set_sessionstatus(::curve::mds::SessionStatus::kSessionOK);

    ::curve::mds::FileInfo* fin = new ::curve::mds::FileInfo;
    fin->set_filename("_filename_");
    fin->set_id(1);
    fin->set_parentid(0);
    fin->set_filetype(curve::mds::FileType::INODE_PAGEFILE);
    fin->set_chunksize(4 * 1024 * 1024);
    fin->set_length(1 * 1024 * 1024 * 1024ul);
    fin->set_ctime(12345678);
    fin->set_seqnum(0);
    fin->set_segmentsize(1 * 1024 * 1024 * 1024ul);
    fin->set_fullpathname("/1_userinfo_");

    openresponse2.set_statuscode(::curve::mds::StatusCode::kOK);
    openresponse2.set_allocated_protosession(se2);
    openresponse2.set_allocated_fileinfo(fin);

    FakeReturn* fakeret2
     = new FakeReturn(nullptr, static_cast<void*>(&openresponse2));
    curvefsservice.SetOpenFile(fakeret2);

    ASSERT_EQ(mdsclient_.OpenFile(filename, UserInfo("test", ""),
                    &finfo, &lease), LIBCURVE_ERROR::OK);

    LOG(INFO) << "create file done!";
    ASSERT_EQ(0, server.Stop(0));
    ASSERT_EQ(0, server.Join());

    delete fakeret;
    delete fakeret1;
    delete fakeret2;
}

TEST_F(MDSClientTest, GetFileInfo) {
    std::string filename = "/1_userinfo_";

    brpc::Server server;

    FakeCurveFSService curvefsservice;

    if (server.AddService(&curvefsservice,
            brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(FATAL) << "Fail to add service";
    }

    // Start the server.
    brpc::ServerOptions options;
    options.idle_timeout_sec = -1;
    if (server.Start(metaserver_addr.c_str(),
        &options) != 0) {
        LOG(ERROR) << "Fail to start Server";
    }

    curve::mds::FileInfo * info = new curve::mds::FileInfo;
    ::curve::mds::GetFileInfoResponse response;
    info->set_filename("_filename_");
    info->set_id(1);
    info->set_parentid(0);
    info->set_filetype(curve::mds::FileType::INODE_PAGEFILE);
    info->set_chunksize(4 * 1024 * 1024);
    info->set_length(4 * 1024 * 1024 * 1024ul);
    info->set_ctime(12345678);
    info->set_segmentsize(1 * 1024 * 1024 * 1024ul);
    info->set_fullpathname("/1_userinfo_");

    response.set_allocated_fileinfo(info);
    response.set_statuscode(::curve::mds::StatusCode::kOK);

    FakeReturn* fakeret =
        new FakeReturn(nullptr, static_cast<void*>(&response));
    curvefsservice.SetFakeReturn(fakeret);

    curve::client::FInfo_t* finfo = new curve::client::FInfo_t;
    mdsclient_.GetFileInfo(filename, UserInfo("test", ""), finfo);

    ASSERT_EQ(finfo->filename, "_filename_");
    ASSERT_EQ(finfo->id, 1);
    ASSERT_EQ(finfo->parentid, 0);
    ASSERT_EQ(static_cast<curve::mds::FileType>(finfo->filetype),
        curve::mds::FileType::INODE_PAGEFILE);
    ASSERT_EQ(finfo->chunksize, 4 * 1024 * 1024);
    ASSERT_EQ(finfo->length, 4 * 1024 * 1024 * 1024ul);
    ASSERT_EQ(finfo->ctime, 12345678);
    ASSERT_EQ(finfo->segmentsize, 1 * 1024 * 1024 * 1024ul);

    // 设置rpc失败，触发重试
    brpc::Controller cntl;
    cntl.SetFailed(-1, "failed");

    FakeReturn* fakeret2
     = new FakeReturn(&cntl, static_cast<void*>(&response));

    curvefsservice.SetFakeReturn(fakeret2);
    curvefsservice.CleanRetryTimes();

    ASSERT_EQ(LIBCURVE_ERROR::FAILED,
         mdsclient_.GetFileInfo(filename.c_str(),
          UserInfo("test", ""), finfo));
    ASSERT_EQ(metaopt.rpcRetryTimes * metaopt.metaaddrvec.size(),
        curvefsservice.GetRetryTimes());

    ASSERT_EQ(0, server.Stop(0));
    ASSERT_EQ(0, server.Join());
    delete fakeret;
    delete fakeret2;
    delete finfo;
}

TEST_F(MDSClientTest, GetOrAllocateSegment) {
    std::string filename = "/1_userinfo_";

    brpc::Server server;

    FakeCurveFSService curvefsservice;
    FakeTopologyService topologyservice;

    if (server.AddService(&curvefsservice,
                          brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(FATAL) << "Fail to add service";
    }
    if (server.AddService(&topologyservice,
                          brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(FATAL) << "Fail to add service";
    }
    brpc::ServerOptions options;
    options.idle_timeout_sec = -1;
    if (server.Start(metaserver_addr.c_str(), &options) != 0) {
        LOG(ERROR) << "Fail to start Server";
    }

    curve::mds::GetOrAllocateSegmentResponse response;
    curve::mds::PageFileSegment* pfs = new curve::mds::PageFileSegment;
    response.set_statuscode(::curve::mds::StatusCode::kOK);
    response.set_allocated_pagefilesegment(pfs);
    response.mutable_pagefilesegment()->set_logicalpoolid(1234);
    response.mutable_pagefilesegment()->set_segmentsize(1*1024*1024*1024ul);
    response.mutable_pagefilesegment()->set_chunksize(4 * 1024 * 1024);
    response.mutable_pagefilesegment()->set_startoffset(0);
    for (int i = 0; i < 256; i ++) {
        auto chunk = response.mutable_pagefilesegment()->add_chunks();
        chunk->set_copysetid(i);
        chunk->set_chunkid(i);
    }
    FakeReturn* fakeret = new FakeReturn(nullptr,
                static_cast<void*>(&response));
    curvefsservice.SetFakeReturn(fakeret);

    ::curve::mds::topology::GetChunkServerListInCopySetsResponse response_1;
    response_1.set_statuscode(0);
    uint64_t chunkserveridc = 1;
    for (int i = 0; i < 256; i ++) {
        auto csinfo = response_1.add_csinfo();
        csinfo->set_copysetid(i);

        for (int j = 0; j < 3; j++) {
            auto cslocs = csinfo->add_cslocs();
            cslocs->set_chunkserverid(chunkserveridc++);
            cslocs->set_hostip("127.0.0.1");
            cslocs->set_port(5000);
        }
    }
    FakeReturn* faktopologyeret = new FakeReturn(nullptr,
        static_cast<void*>(&response_1));
    topologyservice.SetFakeReturn(faktopologyeret);

    curve::client::FInfo_t fi;
    fi.chunksize   = 4 * 1024 * 1024;
    fi.segmentsize = 1 * 1024 * 1024 * 1024ul;
    curve::client::MetaCache mc;
    curve::client::ChunkIDInfo_t cinfo;
    ASSERT_EQ(MetaCacheErrorType::CHUNKINFO_NOT_FOUND,
                mc.GetChunkInfoByIndex(0, &cinfo));

    SegmentInfo segInfo;
    LogicalPoolCopysetIDInfo_t lpcsIDInfo;
    mdsclient_.GetOrAllocateSegment(true, UserInfo("test", ""),
                                    0, &fi, &segInfo);
    int count = 0;
    for (auto iter : segInfo.chunkvec) {
        uint64_t index = (segInfo.startoffset +
                            count*fi.chunksize)/ fi.chunksize;
        mc.UpdateChunkInfoByIndex(index, iter);
        ++count;
    }

    std::vector<CopysetInfo_t> cpinfoVec;
    mdsclient_.GetServerList(segInfo.lpcpIDInfo.lpid,
                            segInfo.lpcpIDInfo.cpidVec, &cpinfoVec);
    for (auto iter : cpinfoVec) {
        mc.UpdateCopysetInfo(segInfo.lpcpIDInfo.lpid,
        iter.cpid_, iter);
    }
    for (int i = 0; i < 256; i++) {
        ASSERT_EQ(MetaCacheErrorType::OK, mc.GetChunkInfoByIndex(i, &cinfo)); // NOLINT
        ASSERT_EQ(cinfo.lpid_, 1234);
        ASSERT_EQ(cinfo.cpid_, i);
        ASSERT_EQ(cinfo.cid_, i);
    }

    curve::client::EndPoint ep;
    butil::str2endpoint("127.0.0.1", 5000, &ep);
    curve::client::PeerId pd(ep);
    for (int i = 0; i < 256; i++) {
        auto serverlist = mc.GetServerList(1234, i);
        ASSERT_TRUE(serverlist.IsValid());
        int chunkserverid = i * 3 + 1;

        uint32_t csid;
        curve::client::EndPoint temp;
        mc.GetLeader(1234, i, &csid, &temp);
        ASSERT_TRUE(csid == chunkserverid);
        ASSERT_TRUE(temp == ep);
        for (auto iter : serverlist.csinfos_) {
            ASSERT_EQ(iter.chunkserverid_, chunkserverid++);
            ASSERT_EQ(pd, iter.peerid_);
        }
    }

    GetChunkServerListInCopySetsResponse response_2;
    response_2.set_statuscode(-1);
    FakeReturn* faktopologyeret_2 = new FakeReturn(nullptr,
        static_cast<void*>(&response_2));
    topologyservice.SetFakeReturn(faktopologyeret_2);

    uint32_t csid;
    curve::client::EndPoint temp;
    ASSERT_EQ(-1, mc.GetLeader(2345, 0, &csid, &temp));

    curve::client::EndPoint ep1;
    butil::str2endpoint("127.0.0.1", 7777, &ep1);
    ChunkServerID cid1 = 4;
    mc.UpdateLeader(1234, 0, &cid1, ep1);

    curve::client::EndPoint toep;
    ChunkServerID cid;
    mc.GetLeader(1234, 0, &cid, &toep, false);

    ASSERT_EQ(ep1, toep);
    ASSERT_EQ(0, mc.UpdateLeader(1234, 0, &cid1, ep1));

    ASSERT_EQ(0, mc.GetAppliedIndex(1111, 0));

    // test applied index update
    curve::client::CopysetInfo_t csinfo;
    mc.UpdateCopysetInfo(111, 123, csinfo);
    ASSERT_EQ(0, mc.GetAppliedIndex(111, 123));
    mc.UpdateAppliedIndex(111, 123, 4);
    ASSERT_EQ(4, mc.GetAppliedIndex(111, 123));
    mc.UpdateAppliedIndex(111, 123, 100000);
    ASSERT_EQ(100000, mc.GetAppliedIndex(111, 123));

    // Boundary test metacache.
    // we fake the disk size = 1G.
    // and the chunksize = 4M.
    // so if visit the chunk index > 255
    // will return failed.
    curve::client::ChunkIDInfo_t chunkinfo;
    ASSERT_EQ(MetaCacheErrorType::CHUNKINFO_NOT_FOUND, mc.GetChunkInfoByIndex(256, &chunkinfo));   // NOLINT
    curve::client::LogicPoolID lpid = 1234;
    curve::client::CopysetID copyid = 0;
    curve::client::PeerId pid;
    curve::client::Configuration conf;
    ASSERT_EQ(-1, ServiceHelper::GetLeader(lpid, copyid, conf, &pid));

    curve::client::EndPoint ep11, ep22, ep33;
    butil::str2endpoint("127.0.0.1", 7777, &ep11);
    curve::client::PeerId pd11(ep11);
    butil::str2endpoint("127.0.0.1", 7777, &ep22);
    curve::client::PeerId pd22(ep22);
    butil::str2endpoint("127.0.0.1", 7777, &ep33);
    curve::client::PeerId pd33(ep33);

    curve::client::Configuration cfg;
    cfg.add_peer(pd11);
    cfg.add_peer(pd22);
    cfg.add_peer(pd33);
    ASSERT_EQ(-1, ServiceHelper::GetLeader(lpid, copyid, conf, &pid));

    ASSERT_EQ(0, server.Stop(0));
    ASSERT_EQ(0, server.Join());
    delete fakeret;
    delete faktopologyeret;
}

TEST_F(MDSClientTest, GetServerList) {
    brpc::Server server;

    FakeTopologyService topologyservice;

    if (server.AddService(&topologyservice,
                          brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(FATAL) << "Fail to add service";
    }
    brpc::ServerOptions options;
    options.idle_timeout_sec = -1;
    if (server.Start(metaserver_addr.c_str(), &options) != 0) {
        LOG(ERROR) << "Fail to start Server";
    }

    ::curve::mds::topology::GetChunkServerListInCopySetsResponse response_1;
    response_1.set_statuscode(0);
    uint32_t chunkserveridc = 1;

    ::curve::mds::topology::ChunkServerLocation* cslocs;
    ::curve::mds::topology::CopySetServerInfo* csinfo;
    for (int j = 0; j < 256; j++) {
        csinfo = response_1.add_csinfo();
        csinfo->set_copysetid(j);
        for (int i = 0; i < 3; i++) {
            cslocs = csinfo->add_cslocs();
            cslocs->set_chunkserverid(chunkserveridc++);
            cslocs->set_hostip("127.0.0.1");
            cslocs->set_port(5000);
        }
    }

    FakeReturn* faktopologyeret = new FakeReturn(nullptr,
        static_cast<void*>(&response_1));
    topologyservice.SetFakeReturn(faktopologyeret);

    std::vector<curve::client::CopysetID> cpidvec;
    for (int i = 0; i < 256; i++) {
        cpidvec.push_back(i);
    }

    std::vector<CopysetInfo_t> cpinfoVec;
    curve::client::MetaCache mc;
    ASSERT_NE(LIBCURVE_ERROR::FAILED,
                mdsclient_.GetServerList(1234, cpidvec, &cpinfoVec));
    for (auto iter : cpinfoVec) {
        mc.UpdateCopysetInfo(1234, iter.cpid_, iter);
    }

    curve::client::EndPoint ep;
    butil::str2endpoint("127.0.0.1", 5000, &ep);
    curve::client::PeerId pd(ep);
    for (int i = 0; i < 256; i++) {
        auto serverlist = mc.GetServerList(1234, i);
        ASSERT_TRUE(serverlist.IsValid());
        int chunkserverid = i * 3 + 1;
        for (auto iter : serverlist.csinfos_) {
            ASSERT_EQ(iter.chunkserverid_, chunkserverid++);
            ASSERT_EQ(pd, iter.peerid_);
        }
    }

    // 设置rpc失败，触发重试
    brpc::Controller cntl;
    cntl.SetFailed(-1, "failed");

    FakeReturn* fakeret2
     = new FakeReturn(&cntl, static_cast<void*>(&response_1));

    topologyservice.SetFakeReturn(fakeret2);
    topologyservice.CleanRetryTimes();

    ASSERT_EQ(LIBCURVE_ERROR::FAILED,
         mdsclient_.GetServerList(12345, cpidvec, &cpinfoVec));
    ASSERT_EQ(metaopt.rpcRetryTimes * metaopt.metaaddrvec.size(),
        topologyservice.GetRetryTimes());

    ASSERT_EQ(0, server.Stop(0));
    ASSERT_EQ(0, server.Join());
    delete faktopologyeret;
    delete fakeret2;
}

TEST_F(MDSClientTest, GetLeaderTest) {
    brpc::Server chunkserver1;
    brpc::Server chunkserver2;
    brpc::Server chunkserver3;

    FakeCliService cliservice;

    if (chunkserver1.AddService(&cliservice,
                          brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(FATAL) << "Fail to add service";
    }

    if (chunkserver2.AddService(&cliservice,
                          brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(FATAL) << "Fail to add service";
    }

    if (chunkserver3.AddService(&cliservice,
                          brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(FATAL) << "Fail to add service";
    }

    brpc::ServerOptions options;
    options.idle_timeout_sec = -1;
    if (chunkserver1.Start("127.0.0.1:7000", &options) != 0) {
        LOG(ERROR) << "Fail to start Server";
    }

    if (chunkserver2.Start("127.0.0.1:7001", &options) != 0) {
        LOG(ERROR) << "Fail to start Server";
    }

    if (chunkserver3.Start("127.0.0.1:7002", &options) != 0) {
        LOG(ERROR) << "Fail to start Server";
    }

    curve::client::EndPoint ep1, ep2, ep3;
    butil::str2endpoint("127.0.0.1", 7000, &ep1);
    curve::client::PeerId pd1(ep1);
    butil::str2endpoint("127.0.0.1", 7001, &ep2);
    curve::client::PeerId pd2(ep2);
    butil::str2endpoint("127.0.0.1", 7002, &ep3);
    curve::client::PeerId pd3(ep3);

    curve::client::Configuration cfg;
    cfg.add_peer(pd1);
    cfg.add_peer(pd2);
    cfg.add_peer(pd3);

    curve::client::MetaCache mc;
    curve::client::CopysetInfo_t cslist;

    curve::client::CopysetPeerInfo_t peerinfo_1;
    peerinfo_1.chunkserverid_ = 1;
    peerinfo_1.peerid_ = pd1;
    cslist.AddCopysetPeerInfo(peerinfo_1);

    curve::client::CopysetPeerInfo_t peerinfo_2;
    peerinfo_2.chunkserverid_ = 2;
    peerinfo_2.peerid_ = pd2;
    cslist.AddCopysetPeerInfo(peerinfo_2);

    curve::client::CopysetPeerInfo_t peerinfo_3;
    peerinfo_3.chunkserverid_ = 3;
    peerinfo_3.peerid_ = pd3;
    cslist.AddCopysetPeerInfo(peerinfo_3);

    mc.UpdateCopysetInfo(1234, 1234, cslist);

    curve::chunkserver::GetLeaderResponse response;
    response.set_leader_id(pd1.to_string());

    FakeReturn fakeret(nullptr, static_cast<void*>(&response));
    cliservice.SetFakeReturn(&fakeret);

    curve::client::ChunkServerID ckid;
    curve::client::EndPoint leaderep;
    mc.GetLeader(1234, 1234, &ckid, &leaderep, true);

    ASSERT_EQ(ckid, 1);
    ASSERT_EQ(ep1, leaderep);

    chunkserver1.Stop(0);
    chunkserver1.Join();
    chunkserver2.Stop(0);
    chunkserver2.Join();
    chunkserver3.Stop(0);
    chunkserver3.Join();
}


TEST_F(MDSClientTest, GetFileInfoException) {
    std::string filename = "/1_userinfo_";

    brpc::Server server;

    FakeCurveFSService curvefsservice;

    if (server.AddService(&curvefsservice,
            brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(FATAL) << "Fail to add service";
    }

    // Start the server.
    brpc::ServerOptions options;
    options.idle_timeout_sec = -1;
    if (server.Start(metaserver_addr.c_str(),
        &options) != 0) {
        LOG(ERROR) << "Fail to start Server";
    }
    FakeReturn* fakeret = nullptr;
    curve::client::FInfo_t* finfo = nullptr;
    {
        curve::mds::FileInfo* info = new curve::mds::FileInfo;
        ::curve::mds::GetFileInfoResponse response;
        response.set_statuscode(::curve::mds::StatusCode::kOK);
        response.set_allocated_fileinfo(info);

        fakeret = new FakeReturn(nullptr,
                static_cast<void*>(&response));
        curvefsservice.SetFakeReturn(fakeret);

        finfo = new curve::client::FInfo_t;
        ASSERT_EQ(LIBCURVE_ERROR::OK,
                mdsclient_.GetFileInfo(filename, UserInfo("test", ""), finfo));
    }

    {
        curve::mds::FileInfo * info = new curve::mds::FileInfo;
        ::curve::mds::GetFileInfoResponse response;
        response.set_statuscode(::curve::mds::StatusCode::kOK);
        info->clear_parentid();
        info->clear_id();
        info->clear_filetype();
        info->clear_chunksize();
        info->clear_length();
        info->clear_ctime();
//        info->clear_snapshotid();
        info->clear_segmentsize();
        response.set_allocated_fileinfo(info);

        fakeret = new FakeReturn(nullptr,
                static_cast<void*>(&response));
        curvefsservice.SetFakeReturn(fakeret);

        finfo = new curve::client::FInfo_t;
        ASSERT_EQ(LIBCURVE_ERROR::OK,
                mdsclient_.GetFileInfo(filename, UserInfo("test", ""), finfo));
    }

    ASSERT_EQ(0, server.Stop(0));
    ASSERT_EQ(0, server.Join());
    delete fakeret;
    delete finfo;
}

TEST_F(MDSClientTest, GetOrAllocateSegmentException) {
    std::string filename = "/1_userinfo_";

    brpc::Server server;

    FakeCurveFSService curvefsservice;
    FakeTopologyService topologyservice;

    if (server.AddService(&curvefsservice,
                          brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(FATAL) << "Fail to add service";
    }
    if (server.AddService(&topologyservice,
                          brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(FATAL) << "Fail to add service";
    }
    brpc::ServerOptions options;
    options.idle_timeout_sec = -1;
    if (server.Start(metaserver_addr.c_str(), &options) != 0) {
        LOG(ERROR) << "Fail to start Server";
    }

    curve::mds::GetOrAllocateSegmentResponse response;
    curve::mds::PageFileSegment* pfs = new curve::mds::PageFileSegment;
    response.set_statuscode(::curve::mds::StatusCode::kOK);
    response.set_allocated_pagefilesegment(pfs);
    FakeReturn* fakeret = new FakeReturn(nullptr,
                static_cast<void*>(&response));
    curvefsservice.SetFakeReturn(fakeret);

    ::curve::mds::topology::GetChunkServerListInCopySetsResponse response_1;
    response_1.set_statuscode(0);
    FakeReturn* faktopologyeret = new FakeReturn(nullptr,
        static_cast<void*>(&response_1));
    topologyservice.SetFakeReturn(faktopologyeret);

    curve::client::FInfo_t fi;
    SegmentInfo segInfo;
    LogicalPoolCopysetIDInfo_t lpcsIDInfo;
    curve::client::MetaCache mc;
    ASSERT_EQ(LIBCURVE_ERROR::FAILED,
            mdsclient_.GetOrAllocateSegment(true, UserInfo("test", ""),
                    0, &fi, &segInfo));


    // 设置rpc失败，触发重试
    brpc::Controller cntl;
    cntl.SetFailed(-1, "failed");

    FakeReturn* fakeret2
     = new FakeReturn(&cntl, static_cast<void*>(&response));
    curvefsservice.SetFakeReturn(fakeret2);

    curvefsservice.CleanRetryTimes();

    ASSERT_EQ(LIBCURVE_ERROR::FAILED,
            mdsclient_.GetOrAllocateSegment(true, UserInfo("test", ""),
                        0, &fi, &segInfo));

    ASSERT_EQ(metaopt.rpcRetryTimes * metaopt.metaaddrvec.size(),
            curvefsservice.GetRetryTimes());

    ASSERT_EQ(0, server.Stop(0));
    ASSERT_EQ(0, server.Join());
    delete fakeret;
    delete faktopologyeret;
}

TEST_F(MDSClientTest, CreateCloneFile) {
    std::string filename = "/1_userinfo_";

    brpc::Server server;

    FakeCurveFSService curvefsservice;

    if (server.AddService(&curvefsservice,
            brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(FATAL) << "Fail to add service";
    }

    // Start the server.
    brpc::ServerOptions options;
    options.idle_timeout_sec = -1;
    if (server.Start(metaserver_addr.c_str(),
        &options) != 0) {
        LOG(ERROR) << "Fail to start Server";
    }

    FInfo finfo;
    curve::mds::FileInfo * info = new curve::mds::FileInfo;

    // 设置rpc失败，触发重试
    brpc::Controller cntl;
    cntl.SetFailed(-1, "failed");

    curve::mds::CreateCloneFileResponse response;
    response.set_statuscode(::curve::mds::StatusCode::kFileNotExists);

    FakeReturn* fakecreateclone
     = new FakeReturn(&cntl, static_cast<void*>(&response));

    curvefsservice.SetCreateCloneFile(fakecreateclone);
    curvefsservice.CleanRetryTimes();

    ASSERT_EQ(LIBCURVE_ERROR::FAILED, mdsclient_.CreateCloneFile("destination",
                                                            UserInfo(),
                                                            10 * 1024 * 1024,
                                                            0,
                                                            4*1024*1024,
                                                            &finfo));

    ASSERT_EQ(metaopt.rpcRetryTimes * metaopt.metaaddrvec.size(),
            curvefsservice.GetRetryTimes());

    // 认证失败
    curve::mds::CreateCloneFileResponse response1;
    response1.set_statuscode(::curve::mds::StatusCode::kOwnerAuthFail);

    FakeReturn* fakecreateclone1
     = new FakeReturn(nullptr, static_cast<void*>(&response1));

    curvefsservice.SetCreateCloneFile(fakecreateclone1);

    ASSERT_EQ(LIBCURVE_ERROR::AUTHFAIL, mdsclient_.CreateCloneFile(
                                                            "destination",
                                                            UserInfo(),
                                                            10 * 1024 * 1024,
                                                            0,
                                                            4*1024*1024,
                                                            &finfo));
    // 请求成功
    info->set_id(5);
    curve::mds::CreateCloneFileResponse response2;
    response2.set_statuscode(::curve::mds::StatusCode::kOK);
    response2.set_allocated_fileinfo(info);

    FakeReturn* fakecreateclone2
     = new FakeReturn(nullptr, static_cast<void*>(&response2));

    curvefsservice.SetCreateCloneFile(fakecreateclone2);

    ASSERT_EQ(LIBCURVE_ERROR::OK, mdsclient_.CreateCloneFile("destination",
                                                            UserInfo(),
                                                            10 * 1024 * 1024,
                                                            0,
                                                            4*1024*1024,
                                                            &finfo));
    ASSERT_EQ(5, finfo.id);
}

TEST_F(MDSClientTest, CompleteCloneMeta) {
    std::string filename = "/1_userinfo_";

    brpc::Server server;

    FakeCurveFSService curvefsservice;

    if (server.AddService(&curvefsservice,
            brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(FATAL) << "Fail to add service";
    }

    // Start the server.
    brpc::ServerOptions options;
    options.idle_timeout_sec = -1;
    if (server.Start(metaserver_addr.c_str(),
        &options) != 0) {
        LOG(ERROR) << "Fail to start Server";
    }

    // 设置rpc失败，触发重试
    brpc::Controller cntl;
    cntl.SetFailed(-1, "failed");

    curve::mds::SetCloneFileStatusResponse response;
    response.set_statuscode(::curve::mds::StatusCode::kFileNotExists);

    FakeReturn* fakecreateclone
     = new FakeReturn(&cntl, static_cast<void*>(&response));

    curvefsservice.SetCloneFileStatus(fakecreateclone);
    curvefsservice.CleanRetryTimes();

    ASSERT_EQ(LIBCURVE_ERROR::FAILED, mdsclient_.CompleteCloneMeta(
                                                            "destination",
                                                            UserInfo()));

    ASSERT_EQ(metaopt.rpcRetryTimes * metaopt.metaaddrvec.size(),
            curvefsservice.GetRetryTimes());

    // 认证失败
    curve::mds::SetCloneFileStatusResponse response1;
    response1.set_statuscode(::curve::mds::StatusCode::kOwnerAuthFail);

    FakeReturn* fakecreateclone1
     = new FakeReturn(nullptr, static_cast<void*>(&response1));

    curvefsservice.SetCloneFileStatus(fakecreateclone1);

    ASSERT_EQ(LIBCURVE_ERROR::AUTHFAIL, mdsclient_.CompleteCloneMeta(
                                                            "destination",
                                                            UserInfo()));
    // 请求成功
    curve::mds::SetCloneFileStatusResponse response2;
    response2.set_statuscode(::curve::mds::StatusCode::kOK);

    FakeReturn* fakecreateclone2
     = new FakeReturn(nullptr, static_cast<void*>(&response2));

    curvefsservice.SetCloneFileStatus(fakecreateclone2);

    ASSERT_EQ(LIBCURVE_ERROR::OK, mdsclient_.CompleteCloneMeta("destination",
                                                            UserInfo()));
}

TEST_F(MDSClientTest, CompleteCloneFile) {
    std::string filename = "/1_userinfo_";

    brpc::Server server;

    FakeCurveFSService curvefsservice;

    if (server.AddService(&curvefsservice,
            brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(FATAL) << "Fail to add service";
    }

    // Start the server.
    brpc::ServerOptions options;
    options.idle_timeout_sec = -1;
    if (server.Start(metaserver_addr.c_str(),
        &options) != 0) {
        LOG(ERROR) << "Fail to start Server";
    }

    // 设置rpc失败，触发重试
    brpc::Controller cntl;
    cntl.SetFailed(-1, "failed");

    curve::mds::SetCloneFileStatusResponse response;
    response.set_statuscode(::curve::mds::StatusCode::kFileNotExists);

    FakeReturn* fakecreateclone
     = new FakeReturn(&cntl, static_cast<void*>(&response));

    curvefsservice.SetCloneFileStatus(fakecreateclone);
    curvefsservice.CleanRetryTimes();

    ASSERT_EQ(LIBCURVE_ERROR::FAILED, mdsclient_.CompleteCloneFile(
                                                            "destination",
                                                            UserInfo()));

    ASSERT_EQ(metaopt.rpcRetryTimes * metaopt.metaaddrvec.size(),
            curvefsservice.GetRetryTimes());

    // 认证失败
    curve::mds::SetCloneFileStatusResponse response1;
    response1.set_statuscode(::curve::mds::StatusCode::kOwnerAuthFail);

    FakeReturn* fakecreateclone1
     = new FakeReturn(nullptr, static_cast<void*>(&response1));

    curvefsservice.SetCloneFileStatus(fakecreateclone1);

    ASSERT_EQ(LIBCURVE_ERROR::AUTHFAIL, mdsclient_.CompleteCloneFile(
                                                            "destination",
                                                            UserInfo()));
    // 请求成功
    curve::mds::SetCloneFileStatusResponse response2;
    response2.set_statuscode(::curve::mds::StatusCode::kOK);

    FakeReturn* fakecreateclone2
     = new FakeReturn(nullptr, static_cast<void*>(&response2));

    curvefsservice.SetCloneFileStatus(fakecreateclone2);

    ASSERT_EQ(LIBCURVE_ERROR::OK, mdsclient_.CompleteCloneFile("destination",
                                                            UserInfo()));
}
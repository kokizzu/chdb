#pragma once

#include "Connection.h"
#include <Interpreters/Context.h>
#include <QueryPipeline/BlockIO.h>
#include <Interpreters/Session.h>
#include <Interpreters/ProfileEventsExt.h>
#include <Storages/ColumnsDescription.h>
#include <Common/CurrentThread.h>


namespace DB
{
class PullingAsyncPipelineExecutor;
class PushingAsyncPipelineExecutor;
class PushingPipelineExecutor;
class QueryPipeline;
class ReadBuffer;

/// State of query processing.
struct LocalQueryState
{
    /// Identifier of the query.
    String query_id;
    QueryProcessingStage::Enum stage = QueryProcessingStage::Complete;

    /// Query text.
    String query;
    /// Streams of blocks, that are processing the query.
    BlockIO io;
    /// Current stream to pull blocks from.
    std::unique_ptr<PullingAsyncPipelineExecutor> executor;
    std::unique_ptr<PushingPipelineExecutor> pushing_executor;
    std::unique_ptr<PushingAsyncPipelineExecutor> pushing_async_executor;
    /// For sending data for input() function.
    std::unique_ptr<QueryPipeline> input_pipeline;
    std::unique_ptr<PullingAsyncPipelineExecutor> input_pipeline_executor;

    InternalProfileEventsQueuePtr profile_queue;
    InternalTextLogsQueuePtr logs_queue;

    std::unique_ptr<Exception> exception;

    /// Current block to be sent next.
    std::optional<Block> block;
    std::optional<ColumnsDescription> columns_description;
    std::optional<ProfileInfo> profile_info;

    /// Is request cancelled
    bool is_cancelled = false;
    bool is_finished = false;

    bool sent_totals = false;
    bool sent_extremes = false;
    bool sent_progress = false;
    bool sent_profile_info = false;
    bool sent_profile_events = false;

    /// To output progress, the difference after the previous sending of progress.
    Progress progress;
    /// Time after the last check to stop the request and send the progress.
    Stopwatch after_send_progress;
    Stopwatch after_send_profile_events;

    std::unique_ptr<CurrentThread::QueryScope> query_scope_holder;
};


class LocalConnection : public IServerConnection, WithContext
{
public:
    explicit LocalConnection(
        ContextPtr context_,
        ReadBuffer * in_,
        bool send_progress_,
        bool send_profile_events_,
        const String & server_display_name_);

    explicit LocalConnection(
        std::unique_ptr<Session> && session_,
        ReadBuffer * in_,
        bool send_progress_ = false,
        bool send_profile_events_ = false,
        const String & server_display_name_ = "");

    ~LocalConnection() override;

    IServerConnection::Type getConnectionType() const override { return IServerConnection::Type::LOCAL; }

    static ServerConnectionPtr createConnection(
        const ConnectionParameters & connection_parameters,
        ContextPtr current_context,
        ReadBuffer * in = nullptr,
        bool send_progress = false,
        bool send_profile_events = false,
        const String & server_display_name = "");

    static ServerConnectionPtr createConnection(
        const ConnectionParameters & connection_parameters,
        std::unique_ptr<Session> && session,
        ReadBuffer * in_,
        bool send_progress = false,
        bool send_profile_events = false,
        const String & server_display_name = "");

    void setDefaultDatabase(const String & database) override;

    void getServerVersion(const ConnectionTimeouts & timeouts,
                          String & name,
                          UInt64 & version_major,
                          UInt64 & version_minor,
                          UInt64 & version_patch,
                          UInt64 & revision) override;

    UInt64 getServerRevision(const ConnectionTimeouts & timeouts) override;
    const String & getServerTimezone(const ConnectionTimeouts & timeouts) override;
    const String & getServerDisplayName(const ConnectionTimeouts & timeouts) override;

    const String & getDescription([[maybe_unused]] bool with_extra = false) const override { return description; }  /// NOLINT

    std::vector<std::pair<String, String>> getPasswordComplexityRules() const override { return {}; }

    void sendQuery(
        const ConnectionTimeouts & timeouts,
        const String & query,
        const NameToNameMap & query_parameters,
        const String & query_id/* = "" */,
        UInt64 stage/* = QueryProcessingStage::Complete */,
        const Settings * settings/* = nullptr */,
        const ClientInfo * client_info/* = nullptr */,
        bool with_pending_data/* = false */,
        const std::vector<String> & external_roles,
        std::function<void(const Progress &)> process_progress_callback) override;

    void sendQueryPlan(const QueryPlan &) override;

    void sendCancel() override;

    void sendData(const Block & block, const String & name/* = "" */, bool scalar/* = false */) override;

    bool isSendDataNeeded() const override;

    void sendExternalTablesData(ExternalTablesData &) override;

    void sendMergeTreeReadTaskResponse(const ParallelReadResponse & response) override;

    bool poll(size_t timeout_microseconds/* = 0 */) override;

    bool hasReadPendingData() const override;

    std::optional<UInt64> checkPacket(size_t timeout_microseconds/* = 0*/) override;

    Packet receivePacket() override;
    UInt64 receivePacketType() override;

    void forceConnected(const ConnectionTimeouts &) override {}

    bool isConnected() const override { return true; }

    bool checkConnected(const ConnectionTimeouts & /*timeouts*/) override { return true; }

    void disconnect() override {}

    void setThrottler(const ThrottlerPtr &) override {}

    const Progress & getCHDBProgress() const { return chdb_progress; }
#if USE_PYTHON
    void resetQueryContext();
#endif

private:
    bool pullBlock(Block & block);

    void finishQuery();

    void updateProgress(const Progress & value);

    void updateCHDBProgress(const Progress & value);

    void sendProfileEvents();

    /// Returns true on executor timeout, meaning a retryable error.
    bool pollImpl();

    bool needSendProgressOrMetrics();
    bool needSendLogs();

    ContextMutablePtr query_context;
    std::unique_ptr<Session> session;

    bool send_progress;
    bool send_profile_events;
    String server_display_name;
    String description = "clickhouse-local";

    std::optional<LocalQueryState> state;

    Progress chdb_progress;

    /// Last "server" packet.
    std::optional<UInt64> next_packet_type;

    String current_database;

    ProfileEvents::ThreadIdToCountersSnapshot last_sent_snapshots;

    ReadBuffer * in;
};

}

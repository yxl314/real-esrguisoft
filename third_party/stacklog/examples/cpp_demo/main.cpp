#include <stacklog/stacklog.hpp>

int main()
{
    stacklog::config cfg;
    cfg.dir = "logs";
    cfg.base_name = "cpp_demo";
    cfg.min_level = stacklog::level::debug;

    if (!stacklog::init(cfg))
    {
        return 1;
    }

    STACKLOG_TRACE("demo-main");
    STACKLOG_INFO() << "cpp demo started";

    {
        STACKLOG_TRACE("demo-work");
        STACKLOG_INFO() << "doing work";
        STACKLOG_DEBUG() << "debug value=" << 42;
    }

    STACKLOG_WARN() << "demo warning";
    stacklog::shutdown();
    return 0;
}

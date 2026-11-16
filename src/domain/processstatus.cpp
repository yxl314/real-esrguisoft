#include "processstatus.h"

#include <QCoreApplication>

QString
process_state_label(ProcessState v_state)
{
	switch (v_state)
	{
		case ProcessState::IDLE:
			return QCoreApplication::translate("ProcessStatus", "待处理");
		case ProcessState::RUNNING:
			return QCoreApplication::translate("ProcessStatus", "处理中");
		case ProcessState::PAUSED:
			return QCoreApplication::translate("ProcessStatus", "已暂停");
		case ProcessState::COMPLETED:
			return QCoreApplication::translate("ProcessStatus", "处理完成");
		case ProcessState::FAILED:
			return QCoreApplication::translate("ProcessStatus", "处理失败");
	}
	return QCoreApplication::translate("ProcessStatus", "待处理");
}

bool
process_is_active(ProcessState v_state)
{
	return v_state == ProcessState::RUNNING || v_state == ProcessState::PAUSED;
}
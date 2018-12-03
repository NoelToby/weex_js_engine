//
// Created by Darin on 23/05/2018.
//

#include <WeexCore/WeexJSServer/task/impl/NativeTimerTask.h>
#include "WeexTaskQueue.h"
#include <WeexCore/WeexJSServer/object/WeexEnv.h>
#include <unistd.h>
#include <WeexCore/WeexJSServer/bridge/script/script_bridge_in_multi_process.h>
#include <WeexCore/WeexJSServer/bridge/script/core_side_in_multi_process.h>

void WeexTaskQueue::run(WeexTask *task) {
    task->timeCalculator->setTaskName(task->taskName());
    task->timeCalculator->taskStart();
    task->run(weexRuntime);
    task->timeCalculator->taskEnd();
    delete task;
}


WeexTaskQueue::~WeexTaskQueue() {
    delete this->weexRuntime;
    weexRuntime = nullptr;
}

int WeexTaskQueue::addTask(WeexTask *task) {
    return _addTask(task, false);
}


WeexTask *WeexTaskQueue::getTask() {
    WeexTask *task = nullptr;
    while (task == nullptr) {
        threadLocker.lock();
        while (taskQueue_.empty() || !isInitOk) {
            threadLocker.wait();
        }

        if (taskQueue_.empty()) {
            threadLocker.unlock();
            continue;
        }

        assert(!taskQueue_.empty());
        task = taskQueue_.front();
        taskQueue_.pop_front();
        threadLocker.unlock();
    }

    return task;
}

int WeexTaskQueue::addTimerTask(String id, uint32_t function, int taskId, WeexGlobalObject* global_object, bool one_shot) {
    WeexTask *task = new NativeTimerTask(id, function,taskId, one_shot);
    task->set_global_object(global_object);
    return _addTask(
            task,
            false);
}

void WeexTaskQueue::removeTimer(int taskId) {
    threadLocker.lock();
    if (taskQueue_.empty()) {
        threadLocker.unlock();
        return;
    } else {
        for (std::deque<WeexTask *>::iterator it = taskQueue_.begin(); it < taskQueue_.end(); ++it) {
            auto reference = *it;
            if (reference->taskId == taskId) {
                NativeTimerTask* timer_task = static_cast<NativeTimerTask*>(reference);
                taskQueue_.erase(it);
                delete (timer_task);
            }
        }
    }
    threadLocker.unlock();
    threadLocker.signal();
}

void WeexTaskQueue::start() {
    while (true) {
        auto pTask = getTask();
        if (pTask == nullptr)
            continue;
        run(pTask);
    }
}


static void *startThread(void *td) {
    auto *self = static_cast<WeexTaskQueue *>(td);
    self->isInitOk = true;

    if (self->weexRuntime == nullptr) {
        self->weexRuntime = new WeexRuntime(WeexEnv::getEnv()->scriptBridge(), self->isMultiProgress);
        // init IpcClient in Js Thread
        if (self->isMultiProgress) {
            auto *client = new WeexIPCClient(WeexEnv::getEnv()->getIpcClientFd());
            static_cast<weex::bridge::js::CoreSideInMultiProcess *>(weex::bridge::js::ScriptBridgeInMultiProcess::Instance()->core_side())->set_ipc_client(
                    client);
        }
        WeexEnv::getEnv()->setTimerQueue(new TimerQueue(self));
    }

    auto pTask = self->getTask();
    self->run(pTask);
    self->start();
    return NULL;
}

void WeexTaskQueue::init() {
    pthread_t thread;
    LOGE("start weex queue init");
    pthread_create(&thread, nullptr, startThread, this);
    pthread_setname_np(thread, "WeexTaskQueueThread");
}

int WeexTaskQueue::_addTask(WeexTask *task, bool front) {
    threadLocker.lock();
    if (front) {
        taskQueue_.push_front(task);
    } else {
        taskQueue_.push_back(task);
    }

    int size = taskQueue_.size();
    threadLocker.unlock();
    threadLocker.signal();
    return size;
}

WeexTaskQueue::WeexTaskQueue(bool isMultiProgress) : weexRuntime(nullptr) {
    this->isMultiProgress = isMultiProgress;
    this->weexRuntime = nullptr;
}

void WeexTaskQueue::removeAllTask(String id) {
    threadLocker.lock();
    if (taskQueue_.empty()) {
        threadLocker.unlock();
        return;
    } else {
        for (std::deque<WeexTask *>::iterator it = taskQueue_.begin(); it < taskQueue_.end(); ++it) {
            auto reference = *it;
            if (reference->instanceId == id) {
                taskQueue_.erase(it);
                delete (reference);
                reference = nullptr;
            }
        }
    }
    threadLocker.unlock();
    threadLocker.signal();
}


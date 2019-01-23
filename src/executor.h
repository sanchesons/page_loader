#pragma once

#include <functional>
#include <list>
#include <thread>

using Task=std::function<bool()>;
using Queue=std::list<Task>;

class Loop
{
private:
    Queue m_queue;

public:

    template<typename T>
    void post(T&& task)
    {
        m_queue.push_back(std::forward<T>(task));
    }

    void run()
    {
        while(!m_queue.empty())
        {
            auto it = m_queue.begin();
            for(; it!=m_queue.end(); ) {

                auto& task=*it;
                if(task()){

                    it=m_queue.erase(it);
                } else {

                    ++it;
                }
            }
            std::this_thread::sleep_for (std::chrono::duration<int,std::ratio<1,1>>());
        }
    }
};
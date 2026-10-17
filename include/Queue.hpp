#pragma once                                                                       

#include <memory>                                                                  
#include <string>

template <typename T>
class QueueBase;

template <typename T>
using QueuePtr = std::shared_ptr<QueueBase<T>>;


/**
 * @brief Creates a new instance of the generic Queue.
 * 
 * @tparam T The type of item (payload) stored in the queue.
 * @return Shared pointer to a newly created queue.
 */
template <typename T>
QueuePtr<T> CreateQueue();

/**
 * @brief Creates a blocking queue for string payloads.
 * @return Shared pointer to a new string queue.
 */
QueuePtr<std::string> CreateStringQueue();

/**
 * @brief Adds an item to the generic Queue.
 * 
 * @tparam T The type of item (payload) stored in the queue.
 * @param queue Queue receiving the item.
 * @param item Shared pointer to the payload to enqueue.
 */
template <typename T>
void AddItem(                                                                    
  QueuePtr<T> queue,                                                      
  std::shared_ptr<T> item                                               
);

/**
 * @brief Retrieves the next item, blocking until the queue is non-empty.
 * 
 * @tparam T The type of item (payload) stored in the queue.
 * @param queue Queue from which to retrieve the item.
 * @return Shared pointer to the retrieved payload/item.
 */
template <typename T>
std::shared_ptr<T> GetItem(                                                   
  QueuePtr<T> queue                                                     
);

/**
 * @brief Gets the number of items currently in the Queue.
 * 
 * @tparam T The type of item (payload) stored in the queue.
 * @param queue Queue to inspect.
 * @return Number of items in the queue.
 */
template <typename T>
size_t GetSize(                                                               
  QueuePtr<T> queue                                                     
);

/**
 * @brief Notifies all threads waiting for a queue item.
 * 
 * @tparam T The type of item (payload) stored in the queue.
 * @param queue Pointer to the work queue.
 */
template <typename T>
void NotifyAll(                                                               
  QueuePtr<T> queue                                                     
);


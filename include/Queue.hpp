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
 * @return QueuePtr<T> Pointer to a newly created Queue.
 */
template <typename T>
QueuePtr<T> CreateQueue();

QueuePtr<std::string> CreateStringQueue();

/**
 * @brief Adds an item to the generic Queue.
 * 
 * @tparam T The type of item (payload) stored in the queue.
 * @param queue - Pointer to the work queue.
 * @param item - Pointer to the payload/item to process.
 */
template <typename T>
void AddItem(                                                                    
  QueuePtr<T> queue,                                                      
  std::shared_ptr<T> item                                               
);

/**
 * @brief Retrieves an item from the generic Queue.
 * 
 * @tparam T The type of item (payload) stored in the queue.
 * @param queue - Pointer to the work queue.
 * @return std::shared_ptr<T> Pointer to the retrieved payload/item.
 */
template <typename T>
std::shared_ptr<T> GetItem(                                                   
  QueuePtr<T> queue                                                     
);

/**
 * @brief Gets the number of items currently in the Queue.
 * 
 * @tparam T The type of item (payload) stored in the queue.
 * @param queue - Pointer to the work queue.
 * @return size_t Number of items in the queue.
 */
template <typename T>
size_t GetSize(                                                               
  QueuePtr<T> queue                                                     
);

/**
 * @brief Called before shutdown to release all listeners/subscribers associated with the queue.
 * 
 * @tparam T The type of item (payload) stored in the queue.
 * @param queue - Pointer to the work queue.
 */
template <typename T>
void NotifyAll(                                                               
  QueuePtr<T> queue                                                     
);


#include "Queue.hpp"

#include <list>                                                                    
#include <mutex>                                                                  
#include <condition_variable>                                                   
#include <memory> // Required for std::shared_ptr and std::make_shared

// Note: We no longer include "EventQueue.hpp" as the class definition is now self-contained 
// and templated, making it more robust.

/**
 * @brief A generic, thread-safe queue implementation for any type T.
 * 
 * This class manages a list of shared pointers to items (T), ensuring safe access 
 * across multiple threads using mutexes and condition variables.
 * 
 * @tparam T The type of the payload/item stored in the queue.
 */
template <typename T>
class QueueBase {                                                                
private:
    std::mutex m_Mutex;                                                        
    std::condition_variable m_Conditional;                                     
    // Storage now holds shared pointers to generic items (T)
    std::list<std::shared_ptr<T>> m_Items;                                      

public:
    QueueBase() {}                                                             
    ~QueueBase() = default; // Use default destructor for simplicity

    /**
     * @brief Adds an item to the queue and notifies one waiting thread.
     * @param item The shared pointer containing the payload/item to be added.
     */
    void AddItem(std::shared_ptr<T> item) {                                       
        if (!item) return; // Safety check for null input
        std::unique_lock<std::mutex> lock(m_Mutex);                               
        m_Items.push_back(item);                                                 
        m_Conditional.notify_one();                                             
    }

    /**
     * @brief Retrieves an item from the queue, blocking until an item is available.
     * 
     * If the queue is empty, this method waits on the condition variable indefinitely.
     * @return std::shared_ptr<T> Pointer to the retrieved payload/item. Returns nullptr if interrupted or signaled while empty (though typically wait handles this).
     */
    std::shared_ptr<T> GetItem() {                                              
        std::unique_lock<std::mutex> lock(m_Mutex);                               
        // Wait until the queue is not empty OR a spurious wakeup occurs.
        m_Conditional.wait(lock, [this] { return !m_Items.empty(); });

        if (m_Items.empty()) {
            return nullptr; // Should only happen if wait was interrupted unexpectedly
        }                                                                    
        
        std::shared_ptr<T> item = m_Items.front();                               // Get the item
        m_Items.pop_front();                                                    // Remove it from the queue
        return item;                                                            
    }

    /**
     * @brief Gets the current number of items in the queue.
     * @return size_t Number of items available.
     */
    size_t GetSize() {                                                         
        std::unique_lock<std::mutex> lock(m_Mutex);                               
        return m_Items.size();                                                  
    }

    /**
     * @brief Notifies all waiting threads that the queue state may have changed.
     */
    void NotifyAll() {                                                        
        m_Conditional.notify_all();                                            
    }
};


// =============================================================================
// TEMPLATED WRAPPER FUNCTIONS (API Layer)
// These functions provide the clean, non-templated interface expected by users.
// =============================================================================

/**
 * @brief Creates a new Queue instance for type T.
 * 
 * @tparam T The type of item stored in the queue.
 * @return std::shared_ptr<QueueBase<T>> Pointer to a newly created Queue.
 */
template <typename T>
std::shared_ptr<QueueBase<T>> CreateQueue() {                                  
  return std::make_shared<QueueBase<T>>();                                   
}

QueuePtr<std::string> CreateStringQueue() {
  return std::make_shared<QueueBase<std::string>>();
}

/**
 * @brief Adds an item to the specified queue.
 * 
 * @tparam T The type of item stored in the queue.
 * @param q - Pointer to the work queue.
 * @param item - Shared pointer containing the payload/item to be added.
 */
template <typename T>
void AddItem(                                                                
  std::shared_ptr<QueueBase<T>> q,                                          
  std::shared_ptr<T> item                                               
) {                                                                          
  q->AddItem(item);                                                      
}

/**
 * @brief Retrieves an item from the specified queue.
 * 
 * @tparam T The type of item stored in the queue.
 * @param q - Pointer to the work queue.
 * @return std::shared_ptr<T> A pointer to the retrieved payload/item.
 */
template <typename T>
std::shared_ptr<T> GetItem(                                               
  std::shared_ptr<QueueBase<T>> q                                        
) {                                                                        
  return q->GetItem();                                                  
}

/**
 * @brief Gets the number of items in the specified queue.
 * 
 * @tparam T The type of item stored in the queue.
 * @param q - Pointer to the work queue.
 * @return size_t Number of items available.
 */
template <typename T>
size_t GetSize(                                                           
  std::shared_ptr<QueueBase<T>> q                                        
) {                                                                        
  return q->GetSize();                                                  
}

/**
 * @brief Notifies all waiting threads associated with the queue.
 * 
 * @tparam T The type of item stored in the queue.
 * @param q - Pointer to the work queue.
 */
template <typename T>
void NotifyAll(                                                           
  std::shared_ptr<QueueBase<T>> q                                        
) {                                                                        
  q->NotifyAll();                                                      
}


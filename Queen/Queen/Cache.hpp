#ifndef Cache_h__
#define Cache_h__

#include <unordered_map>
#include <list>
#include <functional>
#include <cassert>

template <typename K, typename V, int MaxCount>
class LRUCache
{
public:
	typedef K key_type;
	typedef V value_type;

	typedef std::list<key_type> key_tracker_type;
	typedef std::unordered_map<key_type, std::pair<value_type, typename key_tracker_type::iterator> > key_to_value_type;  
	
	LRUCache(const std::function<value_type(key_type)>& func)
		: mValueGenFunc(func)
	{

	}

	//value_type operator() (const key_type& key)
	//{
	//	const typename key_to_value_type::iterator it = mKeyToValues.find(key); 

	//	if (it == mKeyToValues.end())
	//	{
	//		const value_type value = mValueGenFunc(key); 
	//		
	//		insert(key,value); 

	//		// Return the freshly computed value 
	//		return value; 
	//	}
	//	else
	//	{
	//		// Update access record by moving accessed key to back of list 
	//		mkeyTracker.splice(mkeyTracker.end(),  mkeyTracker, (*it).second.second);  
	//
	//		return (*it).second.first;
	//	}
	//}

	const value_type& operator() (const key_type& key) 
	{
		const typename key_to_value_type::const_iterator it = mKeyToValues.find(key); 

		if (it == mKeyToValues.end())
		{
			value_type value = mValueGenFunc(key); 

			insert(key,value); 

			// Return the freshly computed value 
			return mKeyToValues[key].first; 
		}
		else
		{
			// Update access record by moving accessed key to back of list 
			mkeyTracker.splice(mkeyTracker.end(),  mkeyTracker, (*it).second.second);  

			return (*it).second.first;
		}
	}

private:

	void insert(const key_type& k,const value_type& v) { 

		// Method is only called on cache misses 
		assert(mKeyToValues.find(k) == mKeyToValues.end()); 

		// Make space if necessary 
		if (mKeyToValues.size() == Capacity) 
			evict(); 

		// Record k as most-recently-used key 
		typename key_tracker_type::iterator it = mkeyTracker.insert(mkeyTracker.end(), k); 

		// Create the key-value entry, 
		// linked to the usage record. 
		mKeyToValues.insert( 
			std::make_pair( 
			k, 
			std::make_pair(v,it) 
			) 
			); 
		// No need to check return, 
		// given previous assert. 
	} 

	// Purge the least-recently-used element in the cache 
	void evict() { 

		// Assert method is never called when cache is empty 
		assert(!mkeyTracker.empty()); 

		// Identify least recently used key 
		const typename key_to_value_type::iterator it 
			=mKeyToValues.find(mkeyTracker.front()); 
		assert(it!=mKeyToValues.end()); 

		// Erase both elements to completely purge record 
		mKeyToValues.erase(it); 
		mkeyTracker.pop_front(); 
	} 


private:
	key_tracker_type mkeyTracker;
	key_to_value_type mKeyToValues;

	std::function<value_type(key_type)> mValueGenFunc;

	enum { Capacity = MaxCount }; 
};



/**
 * DirectMap Cache, K must be an integer type
 */

template <typename K, typename V, int MaxCount>
class DirectMapCache
{
public:
	typedef K key_type;
	typedef V value_type;

	DirectMapCache(const std::function<value_type(key_type)>& func)
		: mValueGenFunc(func)
	{

	}

	const value_type& operator() (const key_type& key) 
	{
		key_type index = key % Capacity;

		if (mValues[index].key != key)
		{
			mValues[index].key = key;
			mValues[index].value = mValueGenFunc(key); 
		}

		return mValues[index].value;
	}

private:

	enum { Capacity = MaxCount }; 


	struct cache_entry_type
	{
		key_type key;
		value_type value;

		cache_entry_type()
			: key((std::numeric_limits<key_type>::max)())
		{

		}
	};

	std::array<cache_entry_type, Capacity> mValues;

	std::function<value_type(key_type)> mValueGenFunc;
};



#endif // Cache_h__


#include <map>

namespace neon
{
    struct HashTable
    {
        private:
            std::map<Value, Property> m_map;

    };

}
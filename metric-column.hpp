#ifndef METRIC_COLUMN_H_
#define METRIC_COLUMN_H_

#include "stamp.hpp"

#include <string>

/**
 * A Column object represents a thing which knows how to print a column of data.
 * It injects what it wants into the StampConfig object and then gets it back out after.
 *
 * Templated on the result type R.
 */
template <typename R>
class ColumnT {
public:

private:
    const char* header;

protected:

public:
    ColumnT(const char* heading) : header{heading} {}

    virtual ~ColumnT() {}

    virtual std::string get_header() const { return header; }

    /* subclasses can implement this to modify the StampConfig as needed in order to get the values needed
       for this column */
    virtual void update_config(StampConfig& sc) const {}

    std::string get_value(const R& result) const = 0;
};

#endif // #include METRIC_COLUMN_H_
```cpp {.line-numbers}
using FunctionFromUnixTime = FunctionDateTimeStringToString<FromUnixTimeImpl<VecDateTimeValue>>;

template <typename Transform>
class FunctionDateTimeStringToString : public IFunction {
    ...
    Status execute_impl(FunctionContext* context, Block& block, const ColumnNumbers& arguments,
                        size_t result, size_t input_rows_count) const override {
        ...

        if (sources) {
            ...
            if (arguments.size() == 2) {
                const IColumn& source_col1 = *block.get_by_position(arguments[1]).column;
                StringRef formatter =
                        source_col1.get_data_at(0); // for both ColumnString or ColumnConst.
                TransformerToStringTwoArgument<Transform>::vector_constant(
                        context, sources->get_data(), formatter, col_res->get_chars(),
                        col_res->get_offsets(), vec_null_map_to);
            } else { //default argument
                TransformerToStringTwoArgument<Transform>::vector_constant(
                        context, sources->get_data(), StringRef("%Y-%m-%d %H:%i:%s"),
                        col_res->get_chars(), col_res->get_offsets(), vec_null_map_to);
            }

            if (nullable_column) {
                const auto& origin_null_map = nullable_column->get_null_map_column().get_data();
                for (int i = 0; i < origin_null_map.size(); ++i) {
                    vec_null_map_to[i] |= origin_null_map[i];
                }
            }
            block.get_by_position(result).column =
                    ColumnNullable::create(std::move(col_res), std::move(col_null_map_to));
        } else {
            return Status::InternalError("Illegal column {} of first argument of function {}",
                                         block.get_by_position(arguments[0]).column->get_name(),
                                         name);
        }
        return Status::OK();
    }
    ...
}

template <typename Transform>
struct TransformerToStringTwoArgument {
    static void vector_constant(FunctionContext* context,
                                const PaddedPODArray<typename Transform::FromType>& ts,
                                const StringRef& format, ColumnString::Chars& res_data,
                                ColumnString::Offsets& res_offsets,
                                PaddedPODArray<UInt8>& null_map) {
        auto len = ts.size();
        res_offsets.resize(len);
        res_data.reserve(len * format.size + len);
        null_map.resize_fill(len, false);

        size_t offset = 0;
        for (int i = 0; i < len; ++i) {
            const auto& t = ts[i];
            size_t new_offset;
            bool is_null;
            if constexpr (is_specialization_of_v<Transform, FromUnixTimeImpl>) {
                std::tie(new_offset, is_null) = Transform::execute(
                        t, format, res_data, offset, context->state()->timezone_obj());
            } else {
                std::tie(new_offset, is_null) = Transform::execute(t, format, res_data, offset);
            }
            res_offsets[i] = new_offset;
            null_map[i] = is_null;
        }
    }
};

// TODO: This function should be depend on arguments not always nullable
template <typename DateType>
struct FromUnixTimeImpl {
    using FromType = Int64;

    static constexpr auto name = "from_unixtime";

    static inline auto execute(FromType val, StringRef format, ColumnString::Chars& res_data,
                               size_t& offset, const cctz::time_zone& time_zone) {
        DateType dt;
        if (format.size > 128 || val < 0 || val > INT_MAX || !dt.from_unixtime(val, time_zone)) {
            return std::pair {offset, true};
        }

        char buf[128];
        if (!dt.to_format_string(format.data, format.size, buf)) {
            return std::pair {offset, true};
        }

        auto len = strlen(buf);
        res_data.insert(buf, buf + len);
        offset += len;
        return std::pair {offset, false};
    }
};


```
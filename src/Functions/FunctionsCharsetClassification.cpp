#include <Common/FrequencyHolder.h>

#if USE_NLP

#include <Functions/FunctionFactory.h>
#include <Functions/FunctionsTextClassification.h>

#include <memory>


namespace DB
{

namespace
{
    /* We need to solve zero-frequency problem for Naive Bayes Classifier
     * If the bigram is not found in the text, we assume that the probability of its meeting is 1e-06.
     * 1e-06 is minimal value in our marked-up dictionary.
     */
    constexpr Float64 zero_frequency = 1e-06;

    /// If the data size is bigger than this, behaviour is unspecified for this function.
    constexpr size_t max_string_size = 1UL << 15;

    template <typename ModelMap>
    Float64 naiveBayes(
        const FrequencyHolder::EncodingMap & standard,
        const ModelMap & model,
        Float64 max_result)
    {
        Float64 res = 0;
        for (const auto & el : model)
        {
            /// Try to find bigram in the dictionary.
            const auto * it = standard.find(el.getKey());
            if (it != standard.end())
            {
                res += el.getMapped() * log(it->getMapped());
            } else
            {
                res += el.getMapped() * log(zero_frequency);
            }
            /// If at some step the result has become less than the current maximum, then it makes no sense to count it fully.
            if (res < max_result)
            {
                return res;
            }
        }
        return res;
    }

    /// Count how many times each bigram occurs in the text.
    template <typename ModelMap>
    void calculateStats(
        const UInt8 * data,
        const size_t size,
        ModelMap & model)
    {
        UInt16 hash = 0;
        for (size_t i = 0; i < size; ++i)
        {
            hash <<= 8;
            hash += *(data + i);
            ++model[hash];
        }
    }
}

/* Determine language and charset of text data. For each text, we build the distribution of bigrams bytes.
 * Then we use marked-up dictionaries with distributions of bigram bytes of various languages ​​and charsets.
 * Using a naive Bayesian classifier, find the most likely charset and language and return it
 */
template <bool detect_language>
struct CharsetClassificationImpl
{
    static void vector(
        const ColumnString::Chars & data,
        const ColumnString::Offsets & offsets,
        ColumnString::Chars & res_data,
        ColumnString::Offsets & res_offsets,
        size_t input_rows_count)
    {
        const auto & encodings_freq = FrequencyHolder::getInstance().getEncodingsFrequency();

        if constexpr (detect_language)
            /// 2 chars for ISO code + 1 zero byte
            res_data.reserve(input_rows_count * 3);
        else
            /// Mean charset length is 8
            res_data.reserve(input_rows_count * 8);

        res_offsets.resize(input_rows_count);

        size_t current_result_offset = 0;

        double zero_frequency_log = log(zero_frequency);

        for (size_t i = 0; i < input_rows_count; ++i)
        {
            const UInt8 * str = data.data() + offsets[i - 1];
            const size_t str_len = offsets[i] - offsets[i - 1] - 1;

            HashMapWithStackMemory<UInt16, UInt64, DefaultHash<UInt16>, 4> model;
            calculateStats(str, str_len, model);

            std::string_view result_value;

            /// Go through the dictionary and find the charset with the highest weight
            Float64 max_result = zero_frequency_log * (max_string_size);
            for (const auto & item : encodings_freq)
            {
                Float64 score = naiveBayes(item.map, model, max_result);
                if (max_result < score)
                {
                    max_result = score;

                    if constexpr (detect_language)
                        result_value = item.lang;
                    else
                        result_value = item.name;
                }
            }

            size_t result_value_size = result_value.size();
            res_data.resize(current_result_offset + result_value_size + 1);
            memcpy(&res_data[current_result_offset], result_value.data(), result_value_size);
            res_data[current_result_offset + result_value_size] = '\0';
            current_result_offset += result_value_size + 1;

            res_offsets[i] = current_result_offset;
        }
    }
};


struct NameDetectCharset
{
    static constexpr auto name = "detectCharset";
};

struct NameDetectLanguageUnknown
{
    static constexpr auto name = "detectLanguageUnknown";
};


using FunctionDetectCharset = FunctionTextClassificationString<CharsetClassificationImpl<false>, NameDetectCharset>;
using FunctionDetectLanguageUnknown = FunctionTextClassificationString<CharsetClassificationImpl<true>, NameDetectLanguageUnknown>;

REGISTER_FUNCTION(DetectCharset)
{
    factory.registerFunction<FunctionDetectCharset>();
    factory.registerFunction<FunctionDetectLanguageUnknown>();
}

}

#endif

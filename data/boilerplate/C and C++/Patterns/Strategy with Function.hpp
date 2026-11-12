---
label: Strategy with std::function
order: 70
description: A class whose varying behaviour is a std::function, swapped by passing any lambda
includes: <functional> <utility>
---
// The part that varies is a std::function, so any lambda, function or function object can be plugged in
// without a class hierarchy:
//     PriceCalculator calculator;
//     calculator.setDiscount([](double price) { return price * 0.9; }); // 10% off
class PriceCalculator
{
public:
    using Discount = std::function<double(double price)>;

    explicit PriceCalculator(Discount discount = noDiscount) : discount_(std::move(discount)) {}

    void setDiscount(Discount discount) { discount_ = std::move(discount); }
    double finalPrice(double price) const { return discount_(price); }

    static double noDiscount(double price) { return price; }

private:
    Discount discount_;
};

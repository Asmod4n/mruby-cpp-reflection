basket = Showcase::Basket.new("apple")
basket.when_added { |item| puts "added #{item.name.to_s}" }
basket.add("flour", 1.5, Showcase::Unit::Kilogram)
basket.add("salt", 20)

p basket.size
p basket.total_grams
p basket.contents.map { |item| item.name.to_s }

factors = [1.0, 2.0, 3.0]
Showcase.scale(factors, 10)
p factors

p SHOWCASE_VERSION, SHOWCASE_MAX_ITEMS
begin
  Showcase.scale([1.0, 2.0], 2)
rescue ArgumentError => e
  p e
end

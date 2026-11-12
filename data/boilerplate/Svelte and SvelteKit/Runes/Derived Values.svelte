---
label: $derived Values
order: 30
description: A filtered list and a total computed with $derived; they recompute only when their inputs change
---
interface Product {
    name: string;
    price: number;
}

let products = $state<Product[]>([]);
let search = $state('');

let filtered = $derived(products.filter((product) => product.name.toLowerCase().includes(search.toLowerCase())));
let total = $derived(filtered.reduce((sum, product) => sum + product.price, 0));

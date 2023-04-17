// use async_recursion::async_recursion;
// use std::time::Instant;
// use futures::executor::block_on;

// #[async_recursion]
// async fn hanoi(n: i64, f: char, t: char, a: char) -> u64 {
//     if n == 0 {
//         return 0;
//     }
//     return hanoi(n - 1, f, a, t).await + 1 +
//     hanoi(n - 1, a, t, f).await;
// }

// fn main() {
//     for l in 10..21 {
//         block_on(async {
//             let start = Instant::now();
//             let step = hanoi(l, 'a', 'c', 'b').await;
//             println!("l={}, {}, runtime {} us", l, step, start.elapsed().as_micros());
//         });
//     }
// }

/**
use genawaiter::sync::{Gen, GenBoxed};
use std::time::Instant;

fn hanoi(n: i64, f: char, t: char, a: char) -> GenBoxed<u64> {
    Gen::new_boxed(|co| {
        async move {
            if n == 0 {
                return
            }
            for _ in hanoi(n-1, f, a, t) {
                co.yield_(1).await;
            }
            co.yield_(1).await;
            for _ in hanoi(n-1, a, t, f) {
                co.yield_(1).await;
            }
        }
    })
}

fn main() {
    let mut step : u64;
    for l in 1..21 {
        step = 0;
        let start = Instant::now();
        for _ in hanoi(l, 'a', 'c', 'b') {
            step += 1;
        }
        println!("level={}, step {}, runtime {} ns", l, step, start.elapsed().as_nanos());
    }
}
**/
use tokio::{io, task};
use std::time::Instant;

async fn ready_for_io() {
    tokio::task::yield_now().await
}

async fn io_some(_: i64) -> i64 {
    ready_for_io().await;
    return 8;
}

async fn io_fully(mut count: i64) -> i64 {
    let mut size: i64 = 0;
    while count > 0 {
        let s = io_some(count).await;
        match s {
            s if s < 0 => return s,
            _ if s == 0 => return size,
            _ => (),
        }
        count -= s;
        size += s;
    }
    return size;
}

#[tokio::main(flavor = "current_thread")]
// #[tokio::main]
async fn main() -> io::Result<()> {
    // for _i in 0..3 {
        let mut set = task::JoinSet::new();
        let start = Instant::now();
        for _ in 0..10 {
            set.spawn(async move {
                io_fully(10 * 1024 * 1024).await
            });
        }
        while let Some(_res) = set.join_next().await {}
        println!("runtime {} us", start.elapsed().as_micros());
    // }
    Ok(())
}

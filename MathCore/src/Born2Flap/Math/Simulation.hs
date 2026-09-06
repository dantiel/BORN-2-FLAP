{-# LANGUAGE BangPatterns #-}

-- Pure, transactional simulation computation. No IO, clock or global state.
module Born2Flap.Math.Simulation
  ( Simulation, runSimulation, getState, putState, abort ) where

newtype Simulation s e a = Simulation (s -> Either e (a, s))

instance Functor (Simulation s e) where
  fmap f (Simulation run) = Simulation $ \s -> do
    (a, next) <- run s
    pure (f a, next)

instance Applicative (Simulation s e) where
  pure a = Simulation $ \s -> Right (a, s)
  mf <*> ma = do
    f <- mf
    a <- ma
    pure (f a)

instance Monad (Simulation s e) where
  Simulation run >>= f = Simulation $ \s -> do
    (a, next) <- run s
    let Simulation continue = f a
    continue next

runSimulation :: Simulation s e a -> s -> Either e (a, s)
runSimulation (Simulation run) = run

getState :: Simulation s e s
getState = Simulation $ \s -> Right (s, s)

putState :: s -> Simulation s e ()
putState !next = Simulation $ \_ -> Right ((), next)

abort :: e -> Simulation s e a
abort failure = Simulation $ \_ -> Left failure

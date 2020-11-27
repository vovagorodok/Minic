import argparse
import model as M
import nnue_bin_dataset
import pytorch_lightning as pl
from torch import set_num_threads as t_set_num_threads
from pytorch_lightning import loggers as pl_loggers
from torch.utils.data import DataLoader, Dataset

def data_loader_py(train_filename, val_filename, batch_size):
  train = DataLoader(nnue_bin_dataset.NNUEBinData(train_filename), batch_size=batch_size, shuffle=True, num_workers=4)
  val = DataLoader(nnue_bin_dataset.NNUEBinData(val_filename), batch_size=32)
  return train, val

def main():
  parser = argparse.ArgumentParser(description="Trains the network.")
  parser.add_argument("train", help="Training data (.bin or .binpack)")
  parser.add_argument("val", help="Validation data (.bin or .binpack)")
  parser = pl.Trainer.add_argparse_args(parser)
  parser.add_argument("--py-data", action="store_true", help="Use python data loader (default=False)")
  parser.add_argument("--lambda", default=1.0, type=float, dest='lambda_', help="lambda=1.0 = train on evaluations, lambda=0.0 = train on game results, interpolates between (default=1.0).")
  parser.add_argument("--num-workers", default=1, type=int, dest='num_workers', help="Number of worker threads to use for data loading. Currently only works well for binpack.")
  parser.add_argument("--batch-size", default=-1, type=int, dest='batch_size', help="Number of positions per batch / per iteration. Default on GPU = 8192 on CPU = 128.")
  parser.add_argument("--threads", default=-1, type=int, dest='threads', help="Number of torch threads to use. Default automatic (cores) .")
  args = parser.parse_args()

  nnue = M.NNUE(lambda_=args.lambda_)

  print("Training with {} validating with {}".format(args.train, args.val))

  batch_size = args.batch_size
  if batch_size <= 0:
    batch_size = 128 if args.gpus == 0 else 8192
  print('Using batch size {}'.format(batch_size))

  if args.threads > 0:
    print('limiting torch to {} threads.'.format(args.threads))
    t_set_num_threads(args.threads)

  if args.py_data:
    print('Using python data loader')
    train, val = data_loader_py(args.train, args.val, batch_size)
  else:
    print('Using c++ data loader')
    train, val = data_loader_cc(args.train, args.val, args.num_workers, batch_size)

  tb_logger = pl_loggers.TensorBoardLogger('logs/')
  checkpoint_callback = pl.callbacks.ModelCheckpoint(save_last=True)
  trainer = pl.Trainer.from_argparse_args(args, callbacks=[checkpoint_callback], logger=tb_logger)
  trainer.fit(nnue, train, val)

if __name__ == '__main__':
  main()

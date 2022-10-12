class TaintQuery:

    def __init__(self, query_result, panda_taint2, ffi):
        self.num_labels = query_result.num_labels
        self.tcn = query_result.tcn
        self.cb_mask = query_result.cb_mask
        self.qr = query_result
        self.taint2 = panda_taint2
        self.no_more = False
        self.ffi = ffi

    def __iter__(self):
        return self

    def __next__(self):        
        if self.no_more:
            raise StopIteration
        done = self.ffi.new("bool *")
#        print("before calling taint2_query_result_next")
        label = self.taint2.taint2_query_result_next(self.qr, done)
#        print("after calling taint2_query_result_next")
        # this means there aren't any more labels
        # for next time
        if self.ffi.unpack(done,1)[0]:
            self.no_more = True
        return label


    def __str__(self):
        labels = ", ".join(map(str, self.get_labels()))
        return "(n=%d,tcn=%d,cb_mask=%x,labels=(%s))" % (self.num_labels, self.tcn, self.cb_mask, labels)

    def __repr__(self):
        return self.__str__()

    def get_labels(self):
        ret = []
        for l in self:
            ret.append(l)
        #self.reset() # Reset so we can query again
        return ret

    # I think this should reset query result so we can 
    # iterate over labels again
    def reset(self):
        self.taint2.taint2_query_results_iter(self.qr)
        

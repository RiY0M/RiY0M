class ComparisonResult:

    def __init__(self):

        self.identical = []
        self.different = []
        self.only_a = []
        self.only_b = []

    @property
    def identical_count(self):
        return len(self.identical)

    @property
    def different_count(self):
        return len(self.different)

    @property
    def only_a_count(self):
        return len(self.only_a)

    @property
    def only_b_count(self):
        return len(self.only_b)

    @property
    def total_count(self):
        return self.identical_count + self.all_differences_count

    @property
    def all_differences(self):
        return self.different + self.only_a + self.only_b

    @property
    def all_differences_count(self):
        return self.different_count + self.only_a_count + self.only_b_count
